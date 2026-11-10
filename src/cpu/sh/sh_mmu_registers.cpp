#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/sh/sh_mmu.h>
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

class ShMmuRegisters : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kSh; }

    void OnReady() override {
        mmu_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::ShMmu>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kPtehPa; }
    uint32_t MmioSize() const override { return kWindowBytes; }

    uint32_t ReadWord(uint32_t addr) override {
        switch (addr) {
            case kPtehPa: return mmu_->ReadPteh();
            case kPtelPa: return mmu_->ReadPtel();
            case kTtbPa:  return mmu_->ReadTtb();
            case kTeaPa:  return mmu_->ReadTea();
            default: break;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0u);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        switch (addr) {
            case kPtehPa: mmu_->WritePteh(value); return;
            case kPtelPa: mmu_->WritePtel(value); return;
            case kTtbPa:  mmu_->WriteTtb(value); return;
            case kTeaPa:  mmu_->WriteTea(value); return;
            default: break;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

private:
    static constexpr uint32_t kPtehPa      = 0x1FFFFFF0u;
    static constexpr uint32_t kPtelPa      = 0x1FFFFFF4u;
    static constexpr uint32_t kTtbPa       = 0x1FFFFFF8u;
    static constexpr uint32_t kTeaPa       = 0x1FFFFFFCu;
    static constexpr uint32_t kWindowBytes = 0x10u;

    cerfengine::ShMmu* mmu_ = nullptr;
};

}

REGISTER_SERVICE(ShMmuRegisters);

}
