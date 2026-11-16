#include "siemens_simatic_mobile_panels_gen2_f_module_bootloader.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_firmware.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

namespace {

constexpr uint32_t kGpioBBsrr     = 0x40020418u;
constexpr uint32_t kGpioBStatePin = 8u;
constexpr uint32_t kGpioBFailPin  = 9u;
constexpr uint32_t kGpioBErrorPin = 11u;

constexpr uint32_t kIwdgKr    = 0x40003000u;
constexpr uint32_t kIwdgPr    = 0x40003004u;
constexpr uint32_t kIwdgRlr   = 0x40003008u;

constexpr uint32_t kUsartSr   = 0x40011000u;
constexpr uint32_t kUsartDr   = 0x40011004u;
constexpr uint32_t kUsartCr3  = 0x40011014u;
constexpr uint32_t kUsartOre  = 1u << 3;
constexpr uint32_t kUsartRxne = 1u << 5;
constexpr uint32_t kUsartTc   = 1u << 6;
constexpr uint32_t kUsartTxe  = 1u << 7;
constexpr uint32_t kUsartDma  = (1u << 6) | (1u << 7);

constexpr uint32_t kFlashKeyr = 0x40023C04u;
constexpr uint32_t kFlashSr   = 0x40023C0Cu;
constexpr uint32_t kFlashCr   = 0x40023C10u;
constexpr uint32_t kKey1      = 0x45670123u;
constexpr uint32_t kKey2      = 0xCDEF89ABu;
constexpr uint32_t kSrErrors  = 0x000000F2u;
constexpr uint32_t kCrPg      = 1u << 0;
constexpr uint32_t kCrSer     = 1u << 1;
constexpr uint32_t kCrPsize32 = 2u << 8;
constexpr uint32_t kCrStrt    = 1u << 16;
constexpr uint32_t kCrLock    = 1u << 31;

constexpr uint32_t kApplicationBase        = 0x08010000u;
constexpr uint32_t kApplicationBytes       = 0x30000u;
constexpr uint32_t kFirstApplicationSector = 4u;
constexpr uint32_t kLastApplicationSector  = 5u;

constexpr uint32_t kIwdgPrescaler   = 5u;
constexpr uint32_t kIwdgReload      = 2937u;
constexpr int64_t kEndRecordReplyNs = 5000000; /* Between the panel's 2 ms ACK and komp2's 20 ms timeout. */

constexpr uint8_t kTagStart         = 31u;
constexpr uint8_t kTagReady         = 44u;
constexpr uint8_t kTagImage         = 103u;
constexpr uint8_t kTagHeader        = 84u;
constexpr uint8_t kTagRecord        = 50u;
constexpr uint8_t kTagRecordCrc     = 74u;
constexpr uint32_t kRecordBytes     = 136u;
constexpr uint32_t kRecordDataBytes = 128u;

uint32_t PayloadBytes(uint8_t tag) {
    switch (tag) {
    case kTagStart: return 1u;
    case kTagImage: return 6u;
    case kTagHeader: return kRecordBytes;
    case kTagRecord: return kRecordBytes;
    default: return 0u;
    }
}

uint32_t RecordCrc(const uint8_t* data) {
    return Komp2Crc(data, kRecordBytes - 4u);
}

}

void FModuleBootloader::Install(std::vector<uint8_t>& flash) {
    cerf::le::Put32(flash.data(), stm32f2::Mcu::kSramBase + stm32f2::Mcu::kSramBytes);
    cerf::le::Put32(flash.data() + 4u, stm32f2::Mcu::kFlashBase + kBootloaderStubOffset + 1u);
    flash[kBootloaderStubOffset] = 0xFEu;
    flash[kBootloaderStubOffset + 1u] = 0xE7u;
}

void FModuleBootloader::Enter(int64_t now_ns) {
    static_cast<void>(now_ns);
    active_ = true;
    step_ = kResync;
    resync_next_ = false;
    state_ = 0u;
    own_failed_ = false;
    rx_bytes_ = 0u;
    reply_bytes_ = 0u;
    tx_sent_ = 0u;
    sending_ = false;
    sequence_ = 0u;
    end_record_ = false;
    reply_after_ns_ = 0;
    application_target_ = false;
    erase_queued_ = false;
    erase_next_ = 0u;
    program_offset_ = 0u;
    program_left_ = 0u;
    flash_op_ = false;
    erased_ = false;
    records_ = 0u;
    programmed_ = 0u;
    updated_ = false;
    mcu_.Write(kIwdgKr, 4u, 0x5555u);
    mcu_.Write(kIwdgPr, 4u, kIwdgPrescaler);
    mcu_.Write(kIwdgRlr, 4u, kIwdgReload);
    mcu_.Write(kIwdgKr, 4u, 0xCCCCu);
    mcu_.Write(kUsartCr3, 4u, mcu_.Read(kUsartCr3, 4u) & ~kUsartDma);
    mcu_.Read(kUsartSr, 4u);
    mcu_.Read(kUsartDr, 4u);
    Drive(0u);
}

void FModuleBootloader::ServeUsart() {
    const uint32_t sr = mcu_.Read(kUsartSr, 4u);
    if ((sr & kUsartOre) != 0u) mcu_.Fault("F-module bootloader: USART1 overrun", sr);
    if ((sr & kUsartRxne) != 0u) {
        const uint8_t value = static_cast<uint8_t>(mcu_.Read(kUsartDr, 4u));
        if (rx_bytes_ < kMaxFrameBytes) rx_[rx_bytes_] = value;
        ++rx_bytes_;
    }
    if (sending_ && state_ == 2u && tx_sent_ < reply_bytes_ && (sr & kUsartTxe) != 0u)
        mcu_.Write(kUsartDr, 4u, reply_[tx_sent_++]);
}

void FModuleBootloader::StartApplication() {
    active_ = false;
    mcu_.Start(kApplicationBase);
}

void FModuleBootloader::Run(int64_t now_ns) {
    mcu_.Write(kIwdgKr, 4u, 0xAAAAu);
    if (end_record_) {
        end_record_ = false;
        reply_after_ns_ = now_ns + kEndRecordReplyNs;
    }
    if (now_ns < reply_after_ns_) return;
    if (state_ == 0u && (reply_bytes_ == 0u || erase_next_ != 0u) && FlashStep()) return;
    ServeUsart();
    const uint32_t next = step_ == kResync ? (state_ == 1u ? 0u : 1u) : (state_ + 1u) & 3u;
    if (partner_port_b_->PinLevel(kGpioBStatePin) != ((next & 1u) != 0u) || !TransferReady(next)) return;
    Mirror(next);
}

bool FModuleBootloader::TransferReady(uint32_t state) const {
    if (step_ != kTransfer || state != 3u) return true;
    if (sending_) return tx_sent_ == reply_bytes_ && (mcu_.Read(kUsartSr, 4u) & kUsartTc) != 0u;
    return rx_bytes_ >= 2u && rx_bytes_ >= PayloadBytes(rx_[1]) + 4u;
}

void FModuleBootloader::Drive(uint32_t state) {
    state_ = state;
    const uint32_t set = (state & 1u) != 0u ? 1u << kGpioBStatePin : 1u << (kGpioBStatePin + 16u);
    mcu_.Write(kGpioBBsrr, 4u, set | (1u << (kGpioBErrorPin + 16u)));
}

void FModuleBootloader::Mirror(uint32_t state) {
    Drive(state);
    if (step_ == kResync) {
        if (state == 0u) step_ = kTransfer;
        return;
    }
    if (state == 1u) TransferStarted();
    if (state == 3u) {
        own_failed_ = false;
        if (!sending_) {
            const uint32_t bytes = PayloadBytes(rx_[1]);
            if (rx_[0] != 1u || bytes == 0u || rx_bytes_ != bytes + 4u)
                mcu_.Fault("F-module bootloader: frame of an unmodelled kind, tag or length", rx_[1]);
            uint8_t parity = 0u;
            for (uint32_t i = 0u; i < bytes + 3u; ++i) parity ^= rx_[i];
            own_failed_ = parity != rx_[bytes + 3u];
            if (!own_failed_) Accept(rx_[1], rx_.data() + 3u);
        }
        const uint32_t pin = own_failed_ ? kGpioBFailPin : kGpioBFailPin + 16u;
        mcu_.Write(kGpioBBsrr, 4u, 1u << pin);
    }
    if (state == 0u) TransferFinished();
}

void FModuleBootloader::TransferStarted() {
    rx_bytes_ = 0u;
    sending_ = reply_bytes_ != 0u;
    tx_sent_ = 0u;
}

void FModuleBootloader::TransferFinished() {
    const bool partner_failed = partner_port_b_->PinLevel(kGpioBFailPin);
    if (own_failed_ || partner_failed) return;
    if (sending_) {
        reply_bytes_ = 0u;
        sending_ = false;
    }
    if (resync_next_) {
        resync_next_ = false;
        step_ = kResync;
    }
    if (erase_queued_) {
        erase_queued_ = false;
        erase_next_ = kFirstApplicationSector;
        erased_ = true;
        programmed_ = 0u;
        updated_ = false;
    }
}

void FModuleBootloader::QueueReply(uint8_t tag, const uint8_t* data, uint32_t bytes) {
    reply_[0] = 1u;
    reply_[1] = tag;
    reply_[2] = sequence_++;
    std::memcpy(reply_.data() + 3u, data, bytes);
    uint8_t parity = 0u;
    for (uint32_t i = 0u; i < bytes + 3u; ++i) parity ^= reply_[i];
    reply_[bytes + 3u] = parity;
    reply_bytes_ = bytes + 4u;
}

void FModuleBootloader::Accept(uint8_t tag, const uint8_t* data) {
    switch (tag) {
    case kTagStart: {
        const uint8_t ready = 1u;
        QueueReply(kTagReady, &ready, 1u);
        resync_next_ = true;
        erase_queued_ = true;
        return;
    }
    case kTagImage:
        if (std::memcmp(data, "KOMP_3", 6u) == 0)
            mcu_.Fault("F-module bootloader: a bootloader image update is not modelled", 0u);
        if (std::memcmp(data, "KOMP_2", 6u) == 0) application_target_ = true;
        return;
    case kTagHeader:
        if (!application_target_ || RecordCrc(data) != cerf::le::U32(data + kRecordBytes - 4u)) return;
        records_ = cerf::le::U32(data + 0x24u);
        return;
    case kTagRecord: {
        const uint32_t crc = RecordCrc(data);
        uint8_t reply[4];
        cerf::le::Put32(reply, crc);
        QueueReply(kTagRecordCrc, reply, 4u);
        const uint32_t index = cerf::le::U32(data + kRecordDataBytes);
        if (index == 0u) {
            updated_ = erased_ && records_ != 0u && programmed_ == records_;
            end_record_ = true;
            return;
        }
        if (records_ == 0u || !erased_ || crc != cerf::le::U32(data + kRecordBytes - 4u) || index <= programmed_)
            return;
        if (index != programmed_ + 1u) mcu_.Fault("F-module bootloader: record out of order", index);
        program_offset_ = (index - 1u) * kRecordDataBytes;
        if (program_offset_ >= kApplicationBytes)
            mcu_.Fault("F-module bootloader: record beyond the application", index);
        std::memcpy(program_data_.data(), data, kRecordDataBytes);
        program_left_ = kRecordDataBytes;
        return;
    }
    default: mcu_.Fault("F-module bootloader: transfer with an unmodelled tag", tag);
    }
}

bool FModuleBootloader::FlashStep() {
    if (flash_op_) {
        CheckFlash("F-module bootloader: Flash operation failed");
        flash_op_ = false;
    }
    if (erase_next_ != 0u) {
        UnlockFlash();
        mcu_.Write(kFlashCr, 4u, kCrPsize32 | kCrSer | (erase_next_ << 3));
        mcu_.Write(kFlashCr, 4u, kCrPsize32 | kCrSer | (erase_next_ << 3) | kCrStrt);
        flash_op_ = true;
        erase_next_ = erase_next_ == kLastApplicationSector ? 0u : erase_next_ + 1u;
        return true;
    }
    if (program_left_ != 0u) {
        UnlockFlash();
        mcu_.Write(kFlashCr, 4u, kCrPg);
        const uint32_t i = kRecordDataBytes - program_left_;
        mcu_.Write(kApplicationBase + program_offset_ + i, 1u, program_data_[i]);
        flash_op_ = true;
        if (--program_left_ == 0u) ++programmed_;
        return true;
    }
    if ((mcu_.Read(kFlashCr, 4u) & kCrLock) == 0u) LockFlash();
    return false;
}

void FModuleBootloader::UnlockFlash() {
    if ((mcu_.Read(kFlashCr, 4u) & kCrLock) == 0u) return;
    mcu_.Write(kFlashKeyr, 4u, kKey1);
    mcu_.Write(kFlashKeyr, 4u, kKey2);
}

void FModuleBootloader::LockFlash() {
    mcu_.Write(kFlashCr, 4u, kCrLock);
}

void FModuleBootloader::CheckFlash(const char* what) {
    const uint32_t sr = mcu_.Read(kFlashSr, 4u);
    if ((sr & kSrErrors) != 0u) mcu_.Fault(what, sr);
}

void FModuleBootloader::SaveState(StateWriter& writer) const {
    writer.Write("fmodule_boot_active", active_);
    writer.Write("fmodule_boot_step", step_);
    writer.Write("fmodule_boot_resync_next", resync_next_);
    writer.Write("fmodule_boot_state", state_);
    writer.Write("fmodule_boot_own_failed", own_failed_);
    writer.WriteBytes("fmodule_boot_rx", rx_.data(), rx_.size());
    writer.Write("fmodule_boot_rx_bytes", rx_bytes_);
    writer.WriteBytes("fmodule_boot_reply", reply_.data(), reply_.size());
    writer.Write("fmodule_boot_reply_bytes", reply_bytes_);
    writer.Write("fmodule_boot_tx_sent", tx_sent_);
    writer.Write("fmodule_boot_sending", sending_);
    writer.Write("fmodule_boot_sequence", sequence_);
    writer.Write("fmodule_boot_end_record", end_record_);
    writer.Write("fmodule_boot_reply_after_ns", reply_after_ns_);
    writer.Write("fmodule_boot_application_target", application_target_);
    writer.Write("fmodule_boot_erase_queued", erase_queued_);
    writer.Write("fmodule_boot_erase_next", erase_next_);
    writer.WriteBytes("fmodule_boot_program_data", program_data_.data(), program_data_.size());
    writer.Write("fmodule_boot_program_offset", program_offset_);
    writer.Write("fmodule_boot_program_left", program_left_);
    writer.Write("fmodule_boot_flash_op", flash_op_);
    writer.Write("fmodule_boot_erased", erased_);
    writer.Write("fmodule_boot_records", records_);
    writer.Write("fmodule_boot_programmed", programmed_);
    writer.Write("fmodule_boot_updated", updated_);
}

void FModuleBootloader::RestoreState(StateReader& reader) {
    reader.Read("fmodule_boot_active", active_);
    reader.Read("fmodule_boot_step", step_);
    reader.Read("fmodule_boot_resync_next", resync_next_);
    reader.Read("fmodule_boot_state", state_);
    reader.Read("fmodule_boot_own_failed", own_failed_);
    reader.ReadBytes("fmodule_boot_rx", rx_.data(), rx_.size());
    reader.Read("fmodule_boot_rx_bytes", rx_bytes_);
    reader.ReadBytes("fmodule_boot_reply", reply_.data(), reply_.size());
    reader.Read("fmodule_boot_reply_bytes", reply_bytes_);
    reader.Read("fmodule_boot_tx_sent", tx_sent_);
    reader.Read("fmodule_boot_sending", sending_);
    reader.Read("fmodule_boot_sequence", sequence_);
    reader.Read("fmodule_boot_end_record", end_record_);
    reader.Read("fmodule_boot_reply_after_ns", reply_after_ns_);
    reader.Read("fmodule_boot_application_target", application_target_);
    reader.Read("fmodule_boot_erase_queued", erase_queued_);
    reader.Read("fmodule_boot_erase_next", erase_next_);
    reader.ReadBytes("fmodule_boot_program_data", program_data_.data(), program_data_.size());
    reader.Read("fmodule_boot_program_offset", program_offset_);
    reader.Read("fmodule_boot_program_left", program_left_);
    reader.Read("fmodule_boot_flash_op", flash_op_);
    reader.Read("fmodule_boot_erased", erased_);
    reader.Read("fmodule_boot_records", records_);
    reader.Read("fmodule_boot_programmed", programmed_);
    reader.Read("fmodule_boot_updated", updated_);
}

}

}
