#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6BootRom final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && SocId::IsImx6(board->GetSocId());
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return 0x00000000u; }
    uint32_t MmioSize() const override { return 0x00018000u; }

    uint32_t ReadWord(uint32_t addr) override {
        constexpr uint32_t kRomVersionStub = 0x00000015u;
        if (addr == 0x00000048u) return kRomVersionStub;
        HaltUnsupportedAccess("i.MX6 mask-ROM read32", addr, 0);
    }
};

}

REGISTER_SERVICE(Imx6BootRom);

}
