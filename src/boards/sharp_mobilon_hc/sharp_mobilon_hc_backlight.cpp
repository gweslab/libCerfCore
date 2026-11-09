#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../core/board_config.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../backlight_param_id.h"
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class SharpMobilonHcBacklight : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc &&
               emu_.Get<BoardConfig>().EnumId(BoardConfigParamId::Backlight) == "installed";
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            level_   = 0;
            control_ = 0;
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        if (addr - kBase != kLevelOffset) HaltUnsupportedAccess("Sharp HC backlight ReadByte", addr, 0);
        return static_cast<uint8_t>(level_);
    }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kLevelOffset:   return level_;
            case kControlOffset: return control_;
            default: HaltUnsupportedAccess("Sharp HC backlight ReadHalf", addr, 0);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kLevelOffset:   level_ = value;   return;
            case kControlOffset: control_ = value; return;
            default: HaltUnsupportedAccess("Sharp HC backlight WriteHalf", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write("level", level_);
        w.Write("control", control_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("level", level_);
        r.Read("control", control_);
    }

private:
    static constexpr uint32_t kBase          = 0x6D000000u;
    static constexpr uint32_t kSize          = 0x1000u;
    static constexpr uint32_t kLevelOffset   = 0x0u;
    static constexpr uint32_t kControlOffset = 0x2u;

    uint16_t level_   = 0;
    uint16_t control_ = 0;
};

}

REGISTER_SERVICE(SharpMobilonHcBacklight);

}
