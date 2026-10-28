#include <cerfcore/peripherals/peripheral_base.h>
#include "isa_debug_port_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kPortOffset = 0xE0u;

class IsaDebugParallelPort : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.TryGet<IsaDebugPortConfig>() != nullptr;
    }

    void OnReady() override {
        mmio_base_ = emu_.Get<IsaDebugPortConfig>().IsaWindowBase() + kPortOffset;
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { command_ = 0u; });
    }

    uint32_t MmioBase() const override { return mmio_base_; }
    uint32_t MmioSize() const override { return 0x20u; }

    uint8_t ReadByte(uint32_t addr) override {
        switch (addr - mmio_base_) {
            case kStatusOff:  return kNoPeerStatus;
            case kDataInOff:  return kNoPeerData;
            case kCommandOff: return command_;
            default: break;
        }
        HaltUnsupportedAccess("IsaDebugParallelPort ReadByte", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        switch (addr - mmio_base_) {
            case kProbeOff:
                if (value == 0x55u || value == 0xAAu) return;
                break;
            case kDataOutOff:
                return;
            case kStatusOff:
                if (value == kStatusInitStore) return;
                break;
            case kCommandOff: command_ = value; return;
            default: break;
        }
        HaltUnsupportedAccess("IsaDebugParallelPort WriteByte", addr, value);
    }

    void SaveState(StateWriter& w) override { w.Write("command", command_); }
    void RestoreState(StateReader& r) override { r.Read("command", command_); }

private:
    static constexpr uint32_t kProbeOff   = 0x0Eu;
    static constexpr uint32_t kDataOutOff = 0x10u;
    static constexpr uint32_t kDataInOff  = 0x12u;
    static constexpr uint32_t kStatusOff  = 0x14u;
    static constexpr uint32_t kCommandOff = 0x16u;

    static constexpr uint8_t kNoPeerStatus = 0x00u;
    static constexpr uint8_t kNoPeerData   = 0x00u;
    static constexpr uint8_t kStatusInitStore = 0x20u;

    uint32_t mmio_base_ = 0;
    uint8_t  command_   = 0u;
};

}

REGISTER_SERVICE(IsaDebugParallelPort);

}
