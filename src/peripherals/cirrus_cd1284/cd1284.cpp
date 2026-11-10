#include "cd1284.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../tracing/kernel_debug_sink.h"
#include "../peripheral_dispatcher.h"

namespace cerfcore {

namespace {

constexpr uint32_t kCcr    = 0x05u;
constexpr uint32_t kSrer   = 0x06u;
constexpr uint32_t kCor1   = 0x08u;
constexpr uint32_t kCor2   = 0x09u;
constexpr uint32_t kCor3   = 0x0Au;
constexpr uint32_t kCor4   = 0x1Eu;
constexpr uint32_t kCor5   = 0x1Fu;
constexpr uint32_t kPcr    = 0x20u;
constexpr uint32_t kRtpr   = 0x21u;
constexpr uint32_t kOvr    = 0x2Bu;
constexpr uint32_t kIvr    = 0x2Eu;
constexpr uint32_t kGfrcr  = 0x4Fu;
constexpr uint32_t kTdr    = 0x63u;
constexpr uint32_t kSvrr   = 0x67u;
constexpr uint32_t kCar    = 0x68u;
constexpr uint32_t kMir    = 0x69u;
constexpr uint32_t kTir    = 0x6Au;
constexpr uint32_t kRir    = 0x6Bu;
constexpr uint32_t kPcrr   = 0x6Cu;
constexpr uint32_t kGpio   = 0x70u;
constexpr uint32_t kGpdir  = 0x71u;
constexpr uint32_t kTbpr   = 0x72u;
constexpr uint32_t kTcor   = 0x76u;
constexpr uint32_t kRbpr   = 0x78u;
constexpr uint32_t kRcor   = 0x7Cu;
constexpr uint32_t kPpr    = 0x7Eu;

constexpr uint8_t kRevisionE      = 0x25u;
constexpr uint8_t kPprReset       = 0xFFu;
constexpr uint8_t kOvrReset       = 0x48u;
constexpr uint8_t kMirIdle        = 0x08u;
constexpr uint8_t kTirIdle        = 0x10u;
constexpr uint8_t kRirIdle        = 0x18u;
constexpr uint8_t kContextMask    = 0x1Cu;
constexpr uint8_t kIreq           = 0x80u;
constexpr uint8_t kBusy           = 0x40u;
constexpr uint8_t kServiceBits    = 0xC0u;
constexpr uint8_t kChannelMask    = 0x03u;
constexpr uint8_t kCarPollMask    = 0xF8u;
constexpr uint8_t kCarReserved    = 0x04u;
constexpr uint8_t kSvrrTransmit   = 0x02u;
constexpr uint8_t kSvrrExtReceive = 0x10u;
constexpr uint8_t kSvrrExtTransmit = 0x20u;
constexpr uint8_t kSvrrExtModem   = 0x40u;
constexpr uint8_t kSrerTransmit   = 0x06u;
constexpr uint8_t kIvrInputs      = 0x0Fu;
constexpr uint8_t kPcrrReset      = 0x01u;

constexpr uint8_t kCcrReset       = 0x80u;
constexpr uint8_t kCcrResetType   = 0x03u;
constexpr uint8_t kCcrCorChange   = 0x40u;
constexpr uint8_t kCcrCorBits     = 0x0Eu;
constexpr uint8_t kCcrChanCtl     = 0x10u;
constexpr uint8_t kCcrXmtEn       = 0x08u;
constexpr uint8_t kCcrXmtDis      = 0x04u;
constexpr uint8_t kCcrRcvEn       = 0x02u;
constexpr uint8_t kCcrRcvDis      = 0x01u;

constexpr uint8_t kCor1Length     = 0x03u;
constexpr uint8_t kCor1EightBits  = 0x03u;
constexpr uint8_t kCor2TxIbe      = 0x40u;
constexpr uint8_t kCor2Etc        = 0x20u;
constexpr uint8_t kCor2Llm        = 0x10u;
constexpr uint8_t kCor2Rlm        = 0x08u;
constexpr uint8_t kCor2CtsAe      = 0x02u;
constexpr uint8_t kCor2DsrAe      = 0x01u;
constexpr uint8_t kCor2TxControl  = kCor2TxIbe | kCor2Etc | kCor2Llm | kCor2Rlm | kCor2CtsAe | kCor2DsrAe;
constexpr uint8_t kCor5Onlcr      = 0x02u;
constexpr uint8_t kCor5Ocrnl      = 0x01u;
constexpr uint8_t kCor5OutputMap  = kCor5Onlcr | kCor5Ocrnl;

constexpr uint8_t kResetChannel   = 0x00u;
constexpr uint8_t kResetChip      = 0x01u;
constexpr uint8_t kFlushTransmit  = 0x02u;

constexpr uint32_t kFirstSerial   = 2u;
constexpr uint32_t kParallel      = 0u;

constexpr int64_t kChipResetNs    = 1'000;

}

void Cd1284::OnReady() {
    clock_ = &emu_.Get<ExternalDeviceClock>();
    PowerOnReset();
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (kind == ResetLineKind::Rtc) PowerOnReset();
    });
}

void Cd1284::PowerOnReset() {
    for (SerialChannel& s : serial_) s = SerialChannel{};
    car_           = 0u;
    gfrcr_         = kRevisionE;
    gpdir_         = 0u;
    gpio_          = 0u;
    ppr_           = kPprReset;
    pcr_           = 0u;
    ovr_           = kOvrReset;
    pcrr_          = 0u;
    tx_service_    = -1;
    clock_->ClearDeadline(reset_);
}

bool Cd1284::Resetting() { return clock_->Pending(reset_); }

bool Cd1284::TransmitRequest(uint32_t serial) const {
    const SerialChannel& s = serial_[serial];
    return s.tx_enabled && (s.srer & kSrerTransmit) != 0u;
}

int Cd1284::RequestingTransmitter() const {
    for (uint32_t i = 0; i < kSerialCount; ++i) {
        if (TransmitRequest(i)) return static_cast<int>(i);
    }
    return -1;
}

Cd1284::SerialChannel& Cd1284::Selected(uint32_t addr, const char* op, uint8_t value) {
    const uint32_t channel = car_ & kChannelMask;
    if (channel < kFirstSerial) HaltUnsupportedAccess(op, addr, value);
    return serial_[channel - kFirstSerial];
}

uint8_t Cd1284::ReadByte(uint32_t addr) {
    const uint32_t index = (addr - MmioBase()) >> RegisterShift();
    if (((addr - MmioBase()) & ((1u << RegisterShift()) - 1u)) != 0u) HaltUnsupportedAccess("ReadByte", addr, 0u);
    if (Resetting()) {
        if (index != kGfrcr) HaltUnsupportedAccess("ReadByte", addr, 0u);
        return 0u;
    }
    switch (index) {
        case kCar:   return car_;
        case kGpdir: return gpdir_;
        case kGpio:  return static_cast<uint8_t>((gpio_ & gpdir_) | (GpioInputLevels() & ~gpdir_));
        case kPpr:   return ppr_;
        case kMir:   return kMirIdle;
        case kRir:   return kRirIdle;
        case kGfrcr: return gfrcr_;
        case kTir: {
            if (tx_service_ >= 0) {
                return static_cast<uint8_t>(kIreq | kBusy | kTirIdle | (tx_service_ + kFirstSerial));
            }
            const int requesting = RequestingTransmitter();
            if (requesting < 0) return kTirIdle;
            return static_cast<uint8_t>(kIreq | kTirIdle | (requesting + kFirstSerial));
        }
        case kSvrr: {
            const bool transmit = tx_service_ >= 0 || RequestingTransmitter() >= 0;
            return static_cast<uint8_t>(kSvrrExtModem | kSvrrExtReceive | (transmit ? kSvrrTransmit : kSvrrExtTransmit));
        }
        case kCcr:   return 0u;
        default: break;
    }
    if ((car_ & kChannelMask) == kParallel && index == kIvr) {
        return static_cast<uint8_t>(HostHandshakeLevels() & kIvrInputs);
    }
    HaltUnsupportedAccess("ReadByte", addr, 0u);
}

void Cd1284::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t index = (addr - MmioBase()) >> RegisterShift();
    if (((addr - MmioBase()) & ((1u << RegisterShift()) - 1u)) != 0u || Resetting()) {
        HaltUnsupportedAccess("WriteByte", addr, value);
    }
    switch (index) {
        case kGpdir: gpdir_ = value; return;
        case kGpio:  gpio_ = value; return;
        case kPpr:   ppr_ = value; return;
        case kCar:
            if ((value & kCarReserved) != 0u) HaltUnsupportedAccess("WriteByte", addr, value);
            if ((value & kCarPollMask) == 0u) {
                car_ = value;
                return;
            }
            if ((value & (kIreq | kContextMask)) == (kIreq | kTirIdle) && tx_service_ < 0) {
                const uint32_t channel = value & kChannelMask;
                if (channel >= kFirstSerial && TransmitRequest(channel - kFirstSerial)) {
                    tx_service_ = static_cast<int>(channel - kFirstSerial);
                    car_        = value;
                    return;
                }
            }
            HaltUnsupportedAccess("WriteByte", addr, value);
        case kTir:
            if (tx_service_ < 0 || (value & kServiceBits) != 0u ||
                static_cast<uint32_t>(value & kChannelMask) != static_cast<uint32_t>(tx_service_) + kFirstSerial) {
                HaltUnsupportedAccess("WriteByte", addr, value);
            }
            tx_service_ = -1;
            return;
        case kTdr: {
            if (tx_service_ < 0) HaltUnsupportedAccess("WriteByte", addr, value);
            SerialChannel& s = serial_[tx_service_];
            if ((s.cor1 & kCor1Length) != kCor1EightBits || (s.cor2 & kCor2TxControl) != 0u ||
                (s.cor5 & kCor5OutputMap) != 0u) {
                HaltUnsupportedAccess("WriteByte", addr, value);
            }
            emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(value), s.tx_line,
                                                 tx_service_ == 0 ? "CD1284 ch2" : "CD1284 ch3");
            return;
        }
        default: break;
    }
    if ((car_ & kChannelMask) == kParallel) {
        WriteParallel(addr, index, value);
        return;
    }
    WriteSerial(addr, index, value);
}

void Cd1284::WriteParallel(uint32_t addr, uint32_t index, uint8_t value) {
    switch (index) {
        case kCcr: return;
        case kPcr: pcr_ = value; return;
        case kOvr: ovr_ = value; return;
        case kPcrr:
            if ((value & ~kPcrrReset) != 0u) break;
            pcrr_ = value;
            return;
        default: break;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

void Cd1284::WriteSerial(uint32_t addr, uint32_t index, uint8_t value) {
    SerialChannel& s = Selected(addr, "WriteByte", value);
    switch (index) {
        case kCcr:  ChannelCommand(addr, value); return;
        case kSrer: s.srer = value; return;
        case kCor1: s.cor1 = value; return;
        case kCor2: s.cor2 = value; return;
        case kCor3: s.cor3 = value; return;
        case kCor4: s.cor4 = value; return;
        case kCor5: s.cor5 = value; return;
        case kRtpr: s.rtpr = value; return;
        case kTbpr: s.tbpr = value; return;
        case kTcor: s.tcor = value; return;
        case kRbpr: s.rbpr = value; return;
        case kRcor: s.rcor = value; return;
        default: break;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

void Cd1284::ChannelCommand(uint32_t addr, uint8_t value) {
    SerialChannel& s = Selected(addr, "WriteByte", value);
    if ((value & kCcrReset) != 0u) {
        if ((value & ~(kCcrReset | kCcrResetType)) != 0u) HaltUnsupportedAccess("WriteByte", addr, value);
        switch (value & kCcrResetType) {
            case kResetChip:
                PowerOnReset();
                clock_->SetDeadline(reset_, kChipResetNs);
                return;
            case kResetChannel:
                s.tx_enabled = false;
                s.rx_enabled = false;
                return;
            case kFlushTransmit: return;
            default: break;
        }
        HaltUnsupportedAccess("WriteByte", addr, value);
    }
    if ((value & kCcrCorChange) != 0u) {
        if ((value & ~(kCcrCorChange | kCcrCorBits)) != 0u || (value & kCcrCorBits) == 0u) {
            HaltUnsupportedAccess("WriteByte", addr, value);
        }
        return;
    }
    if ((value & kCcrChanCtl) != 0u) {
        if ((value & ~(kCcrChanCtl | kCcrXmtEn | kCcrXmtDis | kCcrRcvEn | kCcrRcvDis)) != 0u ||
            (value & (kCcrXmtEn | kCcrXmtDis)) == (kCcrXmtEn | kCcrXmtDis) ||
            (value & (kCcrRcvEn | kCcrRcvDis)) == (kCcrRcvEn | kCcrRcvDis)) {
            HaltUnsupportedAccess("WriteByte", addr, value);
        }
        if ((value & kCcrXmtEn) != 0u) s.tx_enabled = true;
        if ((value & kCcrXmtDis) != 0u) s.tx_enabled = false;
        if ((value & kCcrRcvEn) != 0u) s.rx_enabled = true;
        if ((value & kCcrRcvDis) != 0u) s.rx_enabled = false;
        return;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

void Cd1284::SaveState(StateWriter& w) {
    for (const SerialChannel& s : serial_) {
        w.Write("srer", s.srer);
        w.Write("cor1", s.cor1);
        w.Write("cor2", s.cor2);
        w.Write("cor3", s.cor3);
        w.Write("cor4", s.cor4);
        w.Write("cor5", s.cor5);
        w.Write("rtpr", s.rtpr);
        w.Write("tbpr", s.tbpr);
        w.Write("tcor", s.tcor);
        w.Write("rbpr", s.rbpr);
        w.Write("rcor", s.rcor);
        w.Write("tx_enabled", s.tx_enabled);
        w.Write("rx_enabled", s.rx_enabled);
    }
    w.Write("car", car_);
    w.Write("gfrcr", gfrcr_);
    w.Write("gpdir", gpdir_);
    w.Write("gpio", gpio_);
    w.Write("ppr", ppr_);
    w.Write("pcr", pcr_);
    w.Write("ovr", ovr_);
    w.Write("pcrr", pcrr_);
    w.Write("tx_service", tx_service_);
    clock_->SaveDeadline(reset_, w, "reset_");
}

void Cd1284::RestoreState(StateReader& r) {
    for (SerialChannel& s : serial_) {
        r.Read("srer", s.srer);
        r.Read("cor1", s.cor1);
        r.Read("cor2", s.cor2);
        r.Read("cor3", s.cor3);
        r.Read("cor4", s.cor4);
        r.Read("cor5", s.cor5);
        r.Read("rtpr", s.rtpr);
        r.Read("tbpr", s.tbpr);
        r.Read("tcor", s.tcor);
        r.Read("rbpr", s.rbpr);
        r.Read("rcor", s.rcor);
        r.Read("tx_enabled", s.tx_enabled);
        r.Read("rx_enabled", s.rx_enabled);
        s.tx_line.clear();
    }
    r.Read("car", car_);
    r.Read("gfrcr", gfrcr_);
    r.Read("gpdir", gpdir_);
    r.Read("gpio", gpio_);
    r.Read("ppr", ppr_);
    r.Read("pcr", pcr_);
    r.Read("ovr", ovr_);
    r.Read("pcrr", pcrr_);
    r.Read("tx_service", tx_service_);
    clock_->RestoreDeadline(reset_, r, "reset_");
}

}
