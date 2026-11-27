#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../irq_controller.h"
#include "msm8255_id.h"
#include "msm8255_qup_i2c_master.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kGsbiBase   = 0xA8300000u;
constexpr uint32_t kWindowSize = 0x1800u;
constexpr uint32_t kQupOffset  = 0x1000u;

constexpr uint32_t kRegGsbiCtrl     = 0x000u;
constexpr uint32_t kRegConfig       = kQupOffset + 0x000u;
constexpr uint32_t kRegState        = kQupOffset + 0x004u;
constexpr uint32_t kRegIoMode       = kQupOffset + 0x008u;
constexpr uint32_t kRegSwReset      = kQupOffset + 0x00Cu;
constexpr uint32_t kRegOperational  = kQupOffset + 0x018u;
constexpr uint32_t kRegErrorFlags   = kQupOffset + 0x01Cu;
constexpr uint32_t kRegErrorFlagsEn = kQupOffset + 0x020u;
constexpr uint32_t kRegMxOutputCnt  = kQupOffset + 0x100u;
constexpr uint32_t kRegOutFifoCnt   = kQupOffset + 0x10Cu;
constexpr uint32_t kRegOutFifo      = kQupOffset + 0x110u;
constexpr uint32_t kRegMxWriteCnt   = kQupOffset + 0x150u;
constexpr uint32_t kRegMxInputCnt   = kQupOffset + 0x200u;
constexpr uint32_t kRegMxReadCnt    = kQupOffset + 0x208u;
constexpr uint32_t kRegInFifoCnt    = kQupOffset + 0x214u;
constexpr uint32_t kRegInFifo       = kQupOffset + 0x218u;
constexpr uint32_t kRegI2cClkCtl    = kQupOffset + 0x400u;
constexpr uint32_t kRegI2cStatus    = kQupOffset + 0x404u;

constexpr uint32_t kGsbiProtocolShift = 4u;
constexpr uint32_t kGsbiProtocolField = 7u << kGsbiProtocolShift;
constexpr uint32_t kGsbiProtocolNone  = 0u;
constexpr uint32_t kGsbiProtocolI2c   = 2u;

constexpr uint32_t kConfigN             = 0x1Fu;
constexpr uint32_t kConfigN16BitItems   = 15u;
constexpr uint32_t kConfigNoOutput      = 1u << 6;
constexpr uint32_t kConfigNoInput       = 1u << 7;
constexpr uint32_t kConfigMiniCoreShift = 8u;
constexpr uint32_t kConfigMiniCoreField = 0xFu << kConfigMiniCoreShift;
constexpr uint32_t kMiniCoreI2cMaster   = 2u;
constexpr uint32_t kConfigClockAutoGate = 1u << 13;
constexpr uint32_t kConfigWritable      = kConfigN | kConfigNoOutput | kConfigNoInput |
                                          kConfigMiniCoreField | kConfigClockAutoGate;

constexpr uint32_t kStateField    = 3u;
constexpr uint32_t kStateReset    = 0u;
constexpr uint32_t kStateRun      = 1u;
constexpr uint32_t kStatePause    = 3u;
constexpr uint32_t kStateValid    = 1u << 2;
constexpr uint32_t kStateMastGen  = 1u << 4;
constexpr uint32_t kStateWritable = kStateField | kStateValid | kStateMastGen;

constexpr uint32_t kBlockSizeField      = 2u;
constexpr uint32_t kFifoSizeField       = 3u;
constexpr uint32_t kIoModeGeometry      = kBlockSizeField | (kFifoSizeField << 2) |
                                          (kBlockSizeField << 5) | (kFifoSizeField << 7);
constexpr uint32_t kIoModeGeometryField = 0x3FFu;
constexpr uint32_t kOutputModeShift     = 10u;
constexpr uint32_t kInputModeShift      = 12u;
constexpr uint32_t kModeField           = 3u;
constexpr uint32_t kModeFifo            = 0u;
constexpr uint32_t kModeBlock           = 1u;
constexpr uint32_t kUnpackEn            = 1u << 14;
constexpr uint32_t kPackEn              = 1u << 15;
constexpr uint32_t kOutputBitShiftEn    = 1u << 16;
constexpr uint32_t kIoModeWritable      = 0x1FC00u;

constexpr uint32_t kBlockBytes = kBlockSizeField * 16u;
constexpr uint32_t kFifoBytes  = kBlockBytes * (2u << kFifoSizeField);
constexpr uint32_t kItemBytes  = 2u;
constexpr uint32_t kFifoItems  = kFifoBytes / kItemBytes;
constexpr uint32_t kBlockItems = kBlockBytes / kItemBytes;

constexpr uint32_t kOutNotEmpty      = 1u << 4;
constexpr uint32_t kInNotEmpty       = 1u << 5;
constexpr uint32_t kOutFull          = 1u << 6;
constexpr uint32_t kInFull           = 1u << 7;
constexpr uint32_t kOutSvc           = 1u << 8;
constexpr uint32_t kInSvc            = 1u << 9;
constexpr uint32_t kMxOutputDone     = 1u << 10;
constexpr uint32_t kMxInputDone      = 1u << 11;
constexpr uint32_t kOutBlockWriteReq = 1u << 12;
constexpr uint32_t kInBlockReadReq   = 1u << 13;
constexpr uint32_t kOperationalLevel = kOutNotEmpty | kInNotEmpty | kOutFull | kInFull;
constexpr uint32_t kOperationalLatch = 0x3F00u;
constexpr uint32_t kOutputEvents     = kOutSvc | kMxOutputDone | kOutBlockWriteReq;
constexpr uint32_t kInputEvents      = kInSvc | kMxInputDone | kInBlockReadReq;

constexpr uint32_t kErrorFlagsField = 0x7Cu;

constexpr uint32_t kItemBits    = 16u;
constexpr uint32_t kItemMask    = 0xFFFFu;
constexpr uint16_t kInDataTag   = 5u << 8;
constexpr uint32_t kClkCtlWritable  = 0x7FFu;
constexpr uint32_t kSwResetCommand  = 1u;
constexpr uint32_t kStatusNack      = 1u << 3;
constexpr uint32_t kStatusBusActive = 1u << 8;
constexpr uint32_t kStatusBusMaster = 1u << 9;
constexpr uint32_t kStatusWritable  = 0xFFFFFCu;

constexpr int kQupInLine  = 102;
constexpr int kQupOutLine = 103;
constexpr int kQupErrLine = 104;

class Msm8255Qup : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        master_ = &emu_.Get<Msm8255QupI2cMaster>();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            gsbi_ctrl_ = 0u;
            ResetQup();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kGsbiBase; }
    uint32_t MmioSize() const override { return kWindowSize; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr - kGsbiBase) {
        case kRegGsbiCtrl:     return gsbi_ctrl_;
        case kRegConfig:       return config_;
        case kRegState:        return State();
        case kRegIoMode:       return kIoModeGeometry | io_mode_;
        case kRegOperational:  return Operational();
        case kRegErrorFlags:   return 0u;
        case kRegErrorFlagsEn: return error_flags_en_;
        case kRegMxOutputCnt:  return mx_output_cnt_;
        case kRegOutFifoCnt:   return out_count_;
        case kRegMxWriteCnt:   return mx_write_cnt_;
        case kRegMxInputCnt:   return mx_input_cnt_;
        case kRegMxReadCnt:    return mx_read_cnt_;
        case kRegInFifoCnt:    return InputWords();
        case kRegInFifo:       return ReadInputWord();
        case kRegI2cClkCtl:    return clk_ctl_;
        case kRegI2cStatus:    return I2cStatus();
        }
        HaltUnsupportedAccess("ReadWord", addr, 0u);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr - kGsbiBase) {
        case kRegGsbiCtrl:     WriteGsbiCtrl(value); return;
        case kRegState:        WriteState(value); return;
        case kRegOutFifo:      PushOutputWord(value); return;
        case kRegOperational:  if (WriteOperational(value)) return; break;
        case kRegConfig:
            if ((value & ~kConfigWritable) != 0u) break;
            config_ = value;
            return;
        case kRegIoMode:
            if ((value & ~(kIoModeGeometryField | kIoModeWritable)) != 0u) break;
            io_mode_ = value & kIoModeWritable;
            return;
        case kRegSwReset:
            if (value != kSwResetCommand) break;
            ResetQup();
            return;
        case kRegErrorFlags:
            if ((value & ~kErrorFlagsField) != 0u) break;
            return;
        case kRegErrorFlagsEn:
            if ((value & ~kErrorFlagsField) != 0u) break;
            error_flags_en_ = value;
            return;
        case kRegMxOutputCnt: mx_output_cnt_ = value; out_items_ = 0u; return;
        case kRegMxWriteCnt:  mx_write_cnt_ = value; out_items_ = 0u; return;
        case kRegMxInputCnt:  mx_input_cnt_ = value; in_items_ = 0u; return;
        case kRegMxReadCnt:   mx_read_cnt_ = value; in_items_ = 0u; return;
        case kRegI2cClkCtl:
            if ((value & ~kClkCtlWritable) != 0u) break;
            clk_ctl_ = value;
            return;
        case kRegI2cStatus:
            if ((value & ~kStatusWritable) != 0u) break;
            if ((value & kStatusNack) != 0u) nack_ = false;
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("qup_gsbi_ctrl", gsbi_ctrl_);
        w.Write<uint32_t>("qup_config", config_);
        w.Write<uint32_t>("qup_state", state_);
        w.Write<uint32_t>("qup_io_mode", io_mode_);
        w.Write<uint32_t>("qup_clk_ctl", clk_ctl_);
        w.Write<uint32_t>("qup_error_flags_en", error_flags_en_);
        w.Write<uint32_t>("qup_operational", operational_);
        w.Write<uint32_t>("qup_mx_output_cnt", mx_output_cnt_);
        w.Write<uint32_t>("qup_mx_write_cnt", mx_write_cnt_);
        w.Write<uint32_t>("qup_mx_input_cnt", mx_input_cnt_);
        w.Write<uint32_t>("qup_mx_read_cnt", mx_read_cnt_);
        w.Write<uint32_t>("qup_out_items", out_items_);
        w.Write<uint32_t>("qup_in_items", in_items_);
        w.Write<uint8_t>("qup_nack", nack_ ? 1u : 0u);
        w.Write("qup_out_fifo", out_fifo_);
        w.Write<uint32_t>("qup_out_count", out_count_);
        w.Write("qup_in_fifo", in_fifo_);
        w.Write<uint32_t>("qup_in_head", in_head_);
        w.Write<uint32_t>("qup_in_count", in_count_);
        master_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        uint8_t nack = 0u;
        r.Read("qup_gsbi_ctrl", gsbi_ctrl_);
        r.Read("qup_config", config_);
        r.Read("qup_state", state_);
        r.Read("qup_io_mode", io_mode_);
        r.Read("qup_clk_ctl", clk_ctl_);
        r.Read("qup_error_flags_en", error_flags_en_);
        r.Read("qup_operational", operational_);
        r.Read("qup_mx_output_cnt", mx_output_cnt_);
        r.Read("qup_mx_write_cnt", mx_write_cnt_);
        r.Read("qup_mx_input_cnt", mx_input_cnt_);
        r.Read("qup_mx_read_cnt", mx_read_cnt_);
        r.Read("qup_out_items", out_items_);
        r.Read("qup_in_items", in_items_);
        r.Read("qup_nack", nack);
        r.Read("qup_out_fifo", out_fifo_);
        r.Read("qup_out_count", out_count_);
        r.Read("qup_in_fifo", in_fifo_);
        r.Read("qup_in_head", in_head_);
        r.Read("qup_in_count", in_count_);
        master_->RestoreState(r);
        nack_ = nack != 0u;
    }

    void PostRestore() override { master_->PostRestore(); }

private:
    uint32_t OutputMode() const { return (io_mode_ >> kOutputModeShift) & kModeField; }
    uint32_t InputMode() const { return (io_mode_ >> kInputModeShift) & kModeField; }
    uint32_t ItemsPerOutputWord() const { return (io_mode_ & kUnpackEn) != 0u ? 2u : 1u; }
    uint32_t ItemsPerInputWord() const { return (io_mode_ & kPackEn) != 0u ? 2u : 1u; }
    uint32_t OutputCapacityWords() const { return kFifoItems / ItemsPerOutputWord(); }

    uint32_t State() const {
        const uint32_t mini_core = (config_ & kConfigMiniCoreField) >> kConfigMiniCoreShift;
        return state_ | kStateValid | (mini_core == kMiniCoreI2cMaster ? kStateMastGen : 0u);
    }

    uint32_t InputWords() const {
        const uint32_t per = ItemsPerInputWord();
        return (in_count_ + per - 1u) / per;
    }

    uint32_t Operational() const {
        uint32_t level = 0u;
        if (out_count_ != 0u) level |= kOutNotEmpty;
        if (in_count_ != 0u) level |= kInNotEmpty;
        if (out_count_ >= OutputCapacityWords()) level |= kOutFull;
        if (in_count_ >= kFifoItems) level |= kInFull;
        return level | operational_;
    }

    uint32_t I2cStatus() const {
        const bool open = master_->BusOpen();
        return (nack_ ? kStatusNack : 0u) | (open ? kStatusBusActive | kStatusBusMaster : 0u);
    }

    bool WriteOperational(uint32_t value) {
        if ((value & ~(kOperationalLevel | kOperationalLatch)) != 0u) return false;
        operational_ &= ~(value & kOperationalLatch);
        return true;
    }

    void WriteGsbiCtrl(uint32_t value) {
        const uint32_t protocol = (value & kGsbiProtocolField) >> kGsbiProtocolShift;
        if ((value & ~kGsbiProtocolField) != 0u ||
            (protocol != kGsbiProtocolNone && protocol != kGsbiProtocolI2c)) {
            HaltUnsupportedAccess("WriteWord", kGsbiBase + kRegGsbiCtrl, value);
        }
        gsbi_ctrl_ = value;
    }

    void WriteState(uint32_t value) {
        if ((value & ~kStateWritable) != 0u) {
            HaltUnsupportedAccess("WriteWord", kGsbiBase + kRegState, value);
        }
        const uint32_t next = value & kStateField;
        if (next == kStateReset) {
            ClearTransfer();
            state_ = kStateReset;
            return;
        }
        const bool run   = next == kStateRun && (state_ == kStateReset || state_ == kStatePause);
        const bool pause = next == kStatePause && state_ == kStateRun;
        if (!run && !pause) {
            emu_.Get<Fatal>().Die("Msm8255Qup: state change %u -> %u is not modeled", state_, next);
        }
        state_ = next;
        if (run) RunOutput();
    }

    void PushOutputWord(uint32_t value) {
        if (state_ == kStateReset || out_count_ >= OutputCapacityWords()) {
            emu_.Get<Fatal>().Die("Msm8255Qup: output FIFO write 0x%08X in state %u with %u words",
                                  value, state_, out_count_);
        }
        out_fifo_[out_count_++] = value;
        if (state_ == kStateRun) RunOutput();
    }

    uint32_t ReadInputWord() {
        if (in_count_ == 0u) {
            emu_.Get<Fatal>().Die("Msm8255Qup: read of an empty input FIFO");
        }
        uint32_t word = PopInputItem();
        if (ItemsPerInputWord() == 2u && in_count_ != 0u) word |= PopInputItem() << kItemBits;
        return word;
    }

    uint32_t PopInputItem() {
        const uint32_t item = in_fifo_[in_head_];
        in_head_  = (in_head_ + 1u) % kFifoItems;
        in_count_ -= 1u;
        return item;
    }

    void RequireI2cConfiguration() {
        const uint32_t protocol  = (gsbi_ctrl_ & kGsbiProtocolField) >> kGsbiProtocolShift;
        const uint32_t mini_core = (config_ & kConfigMiniCoreField) >> kConfigMiniCoreShift;
        const bool     layout_ok = (io_mode_ & (kUnpackEn | kOutputBitShiftEn)) != 0u;
        const bool     modes_ok  = OutputMode() <= kModeBlock && InputMode() <= kModeBlock &&
                                   layout_ok;
        if (protocol != kGsbiProtocolI2c || mini_core != kMiniCoreI2cMaster ||
            (config_ & kConfigN) != kConfigN16BitItems || (config_ & kConfigNoOutput) != 0u ||
            !modes_ok) {
            emu_.Get<Fatal>().Die("Msm8255Qup: transfer with GSBI_CTRL 0x%08X, QUP_CONFIG 0x%08X, "
                                  "QUP_IO_MODE 0x%08X", gsbi_ctrl_, config_,
                                  kIoModeGeometry | io_mode_);
        }
    }

    void RunOutput() {
        if (out_count_ == 0u) {
            Signal(operational_);
            return;
        }
        RequireI2cConfiguration();
        const uint32_t before = operational_;
        std::vector<uint8_t> received;
        bool acked = true;
        for (uint32_t w = 0; w < out_count_ && acked; ++w) {
            const uint32_t word = out_fifo_[w];
            for (uint32_t k = 0; k < ItemsPerOutputWord() && acked; ++k) {
                const uint16_t item = static_cast<uint16_t>((word >> (k * kItemBits)) & kItemMask);
                received.clear();
                acked = master_->Execute(item, received);
                out_items_ += 1u;
                StoreReceived(received);
            }
        }
        out_count_ = 0u;
        if (!acked) nack_ = true;
        if (OutputMode() == kModeFifo) operational_ |= kOutSvc;
        Signal(before);
        if (!acked) emu_.Get<IrqController>().PulseIrq(kQupErrLine);
    }

    void StoreReceived(const std::vector<uint8_t>& bytes) {
        if (!bytes.empty() && (config_ & kConfigNoInput) != 0u) {
            emu_.Get<Fatal>().Die("Msm8255Qup: %zu received bytes with QUP_CONFIG NO_INPUT set",
                                  bytes.size());
        }
        if (in_count_ + bytes.size() > kFifoItems) {
            emu_.Get<Fatal>().Die("Msm8255Qup: %zu received bytes overflow the %u-item input FIFO "
                                  "holding %u", bytes.size(), kFifoItems, in_count_);
        }
        for (uint8_t byte : bytes) {
            in_fifo_[(in_head_ + in_count_) % kFifoItems] = static_cast<uint16_t>(kInDataTag | byte);
            in_count_ += 1u;
            in_items_ += 1u;
        }
    }

    void Signal(uint32_t before) {
        const uint32_t out_target = OutputMode() == kModeBlock ? mx_output_cnt_ : mx_write_cnt_;
        if (out_target != 0u && out_items_ >= out_target) operational_ |= kMxOutputDone;
        if (OutputMode() == kModeBlock && out_items_ < mx_output_cnt_) {
            operational_ |= kOutSvc | kOutBlockWriteReq;
        }
        const uint32_t in_target = InputMode() == kModeBlock ? mx_input_cnt_ : mx_read_cnt_;
        if (in_target != 0u && in_items_ >= in_target) operational_ |= kMxInputDone | kInSvc;
        if (InputMode() == kModeBlock && in_count_ >= kBlockItems) {
            operational_ |= kInSvc | kInBlockReadReq;
        }
        const uint32_t raised = operational_ & ~before;
        auto& intc = emu_.Get<IrqController>();
        if ((raised & kOutputEvents) != 0u) intc.PulseIrq(kQupOutLine);
        if ((raised & kInputEvents) != 0u) intc.PulseIrq(kQupInLine);
    }

    void ClearTransfer() {
        master_->Release();
        operational_ = 0u;
        out_count_   = 0u;
        in_head_     = 0u;
        in_count_    = 0u;
        out_items_   = 0u;
        in_items_    = 0u;
        nack_        = false;
    }

    void ResetQup() {
        ClearTransfer();
        master_->Reset();
        config_         = 0u;
        state_          = kStateReset;
        io_mode_        = 0u;
        clk_ctl_        = 0u;
        error_flags_en_ = 0u;
        mx_output_cnt_  = 0u;
        mx_write_cnt_   = 0u;
        mx_input_cnt_   = 0u;
        mx_read_cnt_    = 0u;
    }

    Msm8255QupI2cMaster* master_ = nullptr;

    uint32_t gsbi_ctrl_      = 0u;
    uint32_t config_         = 0u;
    uint32_t state_          = kStateReset;
    uint32_t io_mode_        = 0u;
    uint32_t clk_ctl_        = 0u;
    uint32_t error_flags_en_ = 0u;
    uint32_t operational_    = 0u;
    uint32_t mx_output_cnt_  = 0u;
    uint32_t mx_write_cnt_   = 0u;
    uint32_t mx_input_cnt_   = 0u;
    uint32_t mx_read_cnt_    = 0u;
    uint32_t out_items_      = 0u;
    uint32_t in_items_       = 0u;
    bool     nack_           = false;

    std::array<uint32_t, kFifoItems> out_fifo_{};
    uint32_t                         out_count_ = 0u;
    std::array<uint16_t, kFifoItems> in_fifo_{};
    uint32_t                         in_head_   = 0u;
    uint32_t                         in_count_  = 0u;
};

}

REGISTER_SERVICE(Msm8255Qup);

}
