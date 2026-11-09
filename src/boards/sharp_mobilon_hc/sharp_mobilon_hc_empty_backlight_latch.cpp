#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../core/board_config.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../backlight_param_id.h"
#include "sharp_mobilon_hc_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

class SharpMobilonHcEmptyBacklightLatch : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc &&
               emu_.Get<BoardConfig>().EnumId(BoardConfigParamId::Backlight) == "none";
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint8_t ReadByte(uint32_t addr) override {
        if (addr - kBase != kLevelOffset) HaltUnsupportedAccess("Sharp HC empty backlight latch ReadByte", addr, 0);
        return 0xFFu;
    }

    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        if (off != kLevelOffset && off != kControlOffset) {
            HaltUnsupportedAccess("Sharp HC empty backlight latch ReadHalf", addr, 0);
        }
        return 0xFFFFu;
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - kBase;
        if (off != kLevelOffset && off != kControlOffset) {
            HaltUnsupportedAccess("Sharp HC empty backlight latch WriteHalf", addr, value);
        }
    }

private:
    static constexpr uint32_t kBase          = 0x6D000000u;
    static constexpr uint32_t kSize          = 0x1000u;
    static constexpr uint32_t kLevelOffset   = 0x0u;
    static constexpr uint32_t kControlOffset = 0x2u;
};

}

REGISTER_SERVICE(SharpMobilonHcEmptyBacklightLatch);

}
