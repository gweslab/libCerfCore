#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "../peripheral_dispatcher.h"
#include "hp_debug_board_placement.h"

namespace cerfcore {

namespace {

constexpr uint32_t kPostLatchOffset = 0x400u;

class HpDebugBoardPostLatch : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return HpDebugBoardPlacement::FittedOnFamily(emu_.Get<BoardContext>().GetBoardFamilyId());
    }

    void OnReady() override {
        base_pa_ = emu_.Get<HpDebugBoardPlacement>().BasePa() + kPostLatchOffset;
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return base_pa_; }
    uint32_t MmioSize() const override { return 4u; }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr != base_pa_) HaltUnsupportedAccess("WriteHalf", addr, value);
        value_ = value;
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr != base_pa_) HaltUnsupportedAccess("WriteWord", addr, value);
        value_ = value;
    }

    void SaveState(StateWriter& w) override { w.Write("value", value_); }

    void RestoreState(StateReader& r) override { r.Read("value", value_); }

private:
    uint32_t base_pa_ = 0u;
    uint32_t value_   = 0u;
};

}

REGISTER_SERVICE(HpDebugBoardPostLatch);

}
