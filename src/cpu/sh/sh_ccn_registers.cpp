#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/sh/sh_cpu.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_mmu.h>
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

class ShCcnRegisters : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kSh; }

    void OnReady() override {
        cerfengine::Engine& engine = emu_.Get<EngineBridge>().Engine();
        state_                     = engine.Get<cerfengine::ShCpu>().State();
        mmu_                       = &engine.Get<cerfengine::ShMmu>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kTraPa; }
    uint32_t MmioSize() const override { return kCcrPa - kTraPa; }

    uint8_t ReadByte(uint32_t addr) override {
        if (addr != kExpevtPa) HaltUnsupportedAccess("ReadByte", addr, 0u);
        return static_cast<uint8_t>(state_->expevt);
    }

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr != kExpevtPa) HaltUnsupportedAccess("ReadHalf", addr, 0u);
        return static_cast<uint16_t>(state_->expevt);
    }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr == kTraPa) return state_->tra;
        if (addr == kExpevtPa) return state_->expevt;
        if (addr == kIntevtPa) return state_->intevt;
        if (addr == kMmucrPa) return mmu_->ReadMmucr();
        HaltUnsupportedAccess("ReadWord", addr, 0u);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr == kTraPa) {
            state_->tra = value & kTraImm;
        } else if (addr == kExpevtPa) {
            state_->expevt = value & kExpevtCode;
        } else if (addr == kMmucrPa) {
            mmu_->WriteMmucr(value);
        } else {
            HaltUnsupportedAccess("WriteWord", addr, value);
        }
    }

private:
    static constexpr uint32_t kTraPa      = 0x1FFFFFD0u;
    static constexpr uint32_t kExpevtPa   = 0x1FFFFFD4u;
    static constexpr uint32_t kIntevtPa   = 0x1FFFFFD8u;
    static constexpr uint32_t kMmucrPa    = 0x1FFFFFE0u;
    static constexpr uint32_t kCcrPa      = 0x1FFFFFECu;
    static constexpr uint32_t kTraImm     = 0x000003FCu;
    static constexpr uint32_t kExpevtCode = 0x00000FFFu;

    cerfengine::ShCpuState* state_ = nullptr;
    cerfengine::ShMmu*      mmu_   = nullptr;
};

}

REGISTER_SERVICE(ShCcnRegisters);

}
