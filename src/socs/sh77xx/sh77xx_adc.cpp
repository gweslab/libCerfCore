#include "sh77xx_adc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "sh77xx_analog_inputs.h"
#include "sh77xx_standby_entry.h"

namespace cerfcore {

namespace {

constexpr uint32_t kOffControlStatus = 0x10u;
constexpr uint32_t kOffControl       = 0x12u;
constexpr uint32_t kDataStride       = 4u;
constexpr uint32_t kDataLowByte      = 2u;

constexpr uint8_t kAdf          = 0x80u;
constexpr uint8_t kAdie         = 0x40u;
constexpr uint8_t kAdst         = 0x20u;
constexpr uint8_t kMulti        = 0x10u;
constexpr uint8_t kChannel      = 0x07u;
constexpr uint8_t kGroup        = 0x04u;
constexpr uint8_t kGroupChannel = 0x03u;
constexpr uint8_t kStoredStatus = 0x5Fu;

constexpr uint8_t kTriggerEnable   = 0xC0u;
constexpr uint8_t kControlReadOnes = 0x3Fu;

constexpr uint8_t kStbcr2Mstp5 = 0x04u;

constexpr const char* kDataNames[4] = {"addra", "addrb", "addrc", "addrd"};

}

void Sh77xxAdc::OnReady() {
    intc_   = &emu_.Get<Sh77xxIntc>();
    dmac_   = &emu_.Get<Sh77xxDmac>();
    stbcr2_ = &emu_.Get<Sh77xxStbcr2>();
    inputs_ = &emu_.Get<Sh77xxAnalogInputs>();
    emu_.Get<Sh77xxStandbyEntry>().RegisterListener([this] { Initialize(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Initialize(); });
    Initialize();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t Sh77xxAdc::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off < kOffControlStatus && (off & 1u) == 0u) return ReadData(off);
    if (off == kOffControlStatus) return ReadControlStatus();
    if (off == kOffControl) return static_cast<uint8_t>(trigger_ | kControlReadOnes);
    HaltUnsupportedAccess("ReadByte", addr, 0u);
}

void Sh77xxAdc::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == kOffControlStatus) {
        WriteControlStatus(value);
        return;
    }
    if (off == kOffControl) {
        trigger_ = static_cast<uint8_t>(value & kTriggerEnable);
        return;
    }
    HaltUnsupportedAccess("WriteByte", addr, value);
}

uint8_t Sh77xxAdc::ReadData(uint32_t offset) {
    const uint16_t data = data_[offset / kDataStride];
    uint8_t        value;
    if ((offset & kDataLowByte) == 0u) {
        value        = static_cast<uint8_t>(data >> 2);
        temp_        = static_cast<uint8_t>((data & 0x3u) << 6);
        temp_loaded_ = true;
    } else {
        if (!temp_loaded_) {
            emu_.Get<Fatal>().Die("Sh77xxAdc: ADDR%c low byte read before any high byte read",
                                  static_cast<int>('A' + offset / kDataStride));
        }
        value = temp_;
    }
    if (dmac_->Transferring(Sh77xxDmaRequest::kAd)) {
        ClearEndFlag();
        Drive();
    }
    return value;
}

uint8_t Sh77xxAdc::ReadControlStatus() {
    if (end_) end_read_ = true;
    return static_cast<uint8_t>((end_ ? kAdf : 0u) | control_);
}

void Sh77xxAdc::WriteControlStatus(uint8_t value) {
    if ((value & kAdf) == 0u && end_read_) ClearEndFlag();
    control_ = static_cast<uint8_t>(value & kStoredStatus);
    if ((value & kAdst) != 0u) Convert(value);
    Drive();
}

void Sh77xxAdc::Convert(uint8_t value) {
    if (stbcr2_->ModuleStopped(kStbcr2Mstp5)) {
        emu_.Get<Fatal>().Die("Sh77xxAdc: ADCSR write 0x%02X starts a conversion while STBCR2 MSTP5 stops the A/D clock",
                              value);
    }
    const uint32_t channel = control_ & kChannel;
    if ((control_ & kMulti) != 0u) {
        const uint32_t first = channel & kGroup;
        for (uint32_t i = 0u; i <= (channel & kGroupChannel); ++i) data_[i] = inputs_->Convert(first + i);
    } else {
        data_[channel & kGroupChannel] = inputs_->Convert(channel);
    }
    end_ = true;
}

void Sh77xxAdc::ClearEndFlag() {
    end_      = false;
    end_read_ = false;
}

bool Sh77xxAdc::Interrupting() const { return end_ && (control_ & kAdie) != 0u; }

void Sh77xxAdc::Drive() {
    if (Interrupting()) {
        dmac_->Request(Sh77xxDmaRequest::kAd);
    } else if (!dmac_->Transferring(Sh77xxDmaRequest::kAd)) {
        dmac_->Withdraw(Sh77xxDmaRequest::kAd);
    }
    intc_->SetSourceRequest(Sh77xxIntcSource::kAdi,
                            Interrupting() && !dmac_->TakesRequest(Sh77xxDmaRequest::kAd));
}

void Sh77xxAdc::Initialize() {
    for (uint16_t& data : data_) data = 0u;
    control_     = 0u;
    trigger_     = 0u;
    temp_loaded_ = false;
    ClearEndFlag();
    Drive();
}

void Sh77xxAdc::SaveState(StateWriter& w) {
    for (uint32_t i = 0u; i < kDataRegisters; ++i) w.Write(kDataNames[i], data_[i]);
    w.Write("control", control_);
    w.Write("trigger", trigger_);
    w.Write("temp", temp_);
    w.Write("end", end_);
    w.Write("end_read", end_read_);
    w.Write("temp_loaded", temp_loaded_);
}

void Sh77xxAdc::RestoreState(StateReader& r) {
    for (uint32_t i = 0u; i < kDataRegisters; ++i) r.Read(kDataNames[i], data_[i]);
    r.Read("control", control_);
    r.Read("trigger", trigger_);
    r.Read("temp", temp_);
    r.Read("end", end_);
    r.Read("end_read", end_read_);
    r.Read("temp_loaded", temp_loaded_);
}

void Sh77xxAdc::PostRestore() { Drive(); }

}
