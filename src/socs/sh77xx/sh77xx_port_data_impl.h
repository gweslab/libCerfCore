#pragma once

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "sh77xx_intc.h"
#include "sh77xx_pfc.h"
#include "sh77xx_port_data.h"
#include "sh77xx_port_wiring.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

namespace sh77xx_port_data {

inline constexpr uint32_t kDataBase  = 0x04000120u;
inline constexpr uint32_t kRegStride = 2u;

inline constexpr const char* kPortNames[kSh77xxPortCount] = {"A", "B", "C", "D", "E", "F",
                                                             "G", "H", "J", "K", "L", "SC"};

struct PortData {
    uint8_t       input_only;
    uint8_t       undefined_other_mode;
    uint8_t       reset;
    uint8_t       function_output;
    uint8_t       separate_input = 0u;
    uint8_t       intc_pins      = 0u;
    Sh77xxIntcPin intc_first     = Sh77xxIntcPin::kCount;
    uint8_t       no_pull_up     = 0u;
};

struct Model {
    PortData port[kSh77xxPortCount];
};

constexpr uint32_t LowestPin(uint8_t mask) {
    uint32_t pin = 0u;
    while (pin < kSh77xxPortPins && ((mask >> pin) & 1u) == 0u) ++pin;
    return pin;
}

template <const std::string_view& Soc, const Model& M>
class Sh77xxPortDataBase : public Sh77xxPortData {
public:
    using Sh77xxPortData::Sh77xxPortData;

    bool ShouldRegister() override { return this->emu_.template Get<BoardContext>().GetSocId() == Soc; }

    void OnReady() override {
        pfc_    = &this->emu_.template Get<Sh77xxPfc>();
        intc_   = &this->emu_.template Get<Sh77xxIntc>();
        wiring_ = &this->emu_.template Get<Sh77xxPortWiring>();
        ResetLatches();
        this->emu_.template Get<PeripheralDispatcher>().Register(this);
        GuestCpuReset& reset = this->emu_.template Get<GuestCpuReset>();
        reset.RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) ResetLatches();
        });
        reset.RegisterResetReleaseListener([this] { TrackDrive(); });
        pfc_->AddControlListener([this] {
            RequireIntcPinsUndriven();
            TrackDrive();
        });
        TrackDrive();
    }

    uint32_t MmioBase() const override { return kDataBase; }
    uint32_t MmioSize() const override { return kSh77xxPortCount * kRegStride; }

    PinDrive Drive(uint32_t port, uint32_t pin) const override {
        const PortData& p    = M.port[port];
        const uint8_t   bit  = static_cast<uint8_t>(1u << pin);
        const uint32_t  mode = pfc_->PinMode(port, pin);
        if (mode == kSh77xxPinModeOutput) {
            if ((p.input_only & bit) != 0u) return PinDrive::kUndriven;
            return (latch_[port] & bit) != 0u ? PinDrive::kHigh : PinDrive::kLow;
        }
        if ((p.separate_input & bit) != 0u) return PinDrive::kUndriven;
        switch (mode) {
            case kSh77xxPinModeOther:
                return (p.function_output & bit) != 0u ? PinDrive::kFunction : PinDrive::kUndriven;
            case kSh77xxPinModePullUpInput:
                return (p.no_pull_up & bit) != 0u ? PinDrive::kUndriven : PinDrive::kPulledUp;
            default:                        return PinDrive::kUndriven;
        }
    }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t  port  = Port(addr, "ReadByte", 0u);
        const PortData& p     = M.port[port];
        const uint8_t   wired = wiring_->WiredInputs(port);
        const uint8_t   nets  = wiring_->NetLevels(port);
        uint8_t         v     = 0u;
        for (uint32_t pin = 0; pin < kSh77xxPortPins; ++pin) {
            const uint32_t mode = pfc_->PinMode(port, pin);
            const uint8_t  bit  = static_cast<uint8_t>(1u << pin);
            if (mode >= kSh77xxPinModeInputFirst) {
                if ((p.separate_input & bit) != 0u) {
                    this->emu_.template Get<Fatal>().Die(
                        "Sh77xxPortData: read of port %s pin %u from its separate input pin", kPortNames[port], pin);
                }
                if ((wired & bit) == 0u) {
                    this->emu_.template Get<Fatal>().Die("Sh77xxPortData: read of port %s pin %u in input mode %u",
                                                         kPortNames[port], pin, mode);
                }
                const bool high = (p.intc_pins & bit) != 0u ? intc_->PinLevel(IntcPin(p, pin)) : (nets & bit) != 0u;
                if (high) v = static_cast<uint8_t>(v | bit);
                continue;
            }
            if (mode == kSh77xxPinModeOther && (p.undefined_other_mode & bit) != 0u) {
                this->emu_.template Get<Fatal>().Die("Sh77xxPortData: read of port %s pin %u in mode 0",
                                                     kPortNames[port], pin);
            }
            if ((p.input_only & bit) == 0u) v = static_cast<uint8_t>(v | (latch_[port] & bit));
        }
        return v;
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t  port = Port(addr, "WriteByte", value);
        const PortData& p    = M.port[port];
        for (uint32_t pin = 0; pin < kSh77xxPortPins; ++pin) {
            const uint8_t bit = static_cast<uint8_t>(1u << pin);
            if ((p.undefined_other_mode & bit) != 0u && pfc_->PinMode(port, pin) == kSh77xxPinModeOther) {
                this->emu_.template Get<Fatal>().Die("Sh77xxPortData: write 0x%02X to port %s with pin %u in mode 0",
                                                     value, kPortNames[port], pin);
            }
        }
        latch_[port] = static_cast<uint8_t>(value & ~p.input_only);
        TrackDrive();
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("latch", latch_, sizeof(latch_));
        wiring_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        r.ReadBytes("latch", latch_, sizeof(latch_));
        wiring_->RestoreState(r);
    }

    void PostRestore() override { TrackDrive(); }

private:
    void TrackDrive() {
        wiring_->TrackDrive(*this);
        this->NotifyDriveChanged();
    }

    static Sh77xxIntcPin IntcPin(const PortData& p, uint32_t pin) {
        return static_cast<Sh77xxIntcPin>(static_cast<uint32_t>(p.intc_first) + pin - LowestPin(p.intc_pins));
    }

    void RequireIntcPinsUndriven() {
        for (uint32_t port = 0; port < kSh77xxPortCount; ++port) {
            for (uint32_t pin = 0; pin < kSh77xxPortPins; ++pin) {
                if (((M.port[port].intc_pins >> pin) & 1u) == 0u) continue;
                const PinDrive drive = Drive(port, pin);
                if (drive != PinDrive::kPulledUp && drive != PinDrive::kUndriven) {
                    this->emu_.template Get<Fatal>().Die("Sh77xxPortData: port %s pin %u drives its INTC input",
                                                         kPortNames[port], pin);
                }
            }
        }
    }

    void ResetLatches() {
        for (uint32_t i = 0; i < kSh77xxPortCount; ++i) {
            latch_[i] = static_cast<uint8_t>(M.port[i].reset & ~M.port[i].input_only);
        }
    }

    uint32_t Port(uint32_t addr, const char* op, uint32_t value) {
        const uint32_t off = addr - kDataBase;
        if ((off % kRegStride) != 0u) this->HaltUnsupportedAccess(op, addr, value);
        return off / kRegStride;
    }

    Sh77xxPfc*        pfc_                     = nullptr;
    Sh77xxIntc*       intc_                    = nullptr;
    Sh77xxPortWiring* wiring_                  = nullptr;
    uint8_t           latch_[kSh77xxPortCount] = {};
};

}

}
