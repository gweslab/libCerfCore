#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../peripherals/nec_vrc4172/vrc4172_chip_selected.h"
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kModemChipSelect = 2u;

class Vr4102HardModemUart : public Vrc4172ChipSelected<Uart16550> {
public:
    explicit Vr4102HardModemUart(CerfEmulator& emu)
        : Vrc4172ChipSelected<Uart16550>(emu, Config{0x0Fu, 0x08u, 0u}, Clock{1843200u, 1u}) {}

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    uint32_t MmioBase() const override { return 0x17100000u; }
    uint32_t MmioSize() const override { return 0x08u; }   /* 8 regs, stride 1 */

    void OnChipSelectReady() override {
        Uart16550::OnReady();

        Port().AttachCradle(emu_, "COM4 (internal modem)");
        LOG(Periph, "[UART] HardModem attached as serial endpoint line 'COM4'\n");
    }

protected:
    uint32_t    ChipSelect() const override { return kModemChipSelect; }
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "HardModem"; }

    void SetInterruptLine(bool pending) override {
        /* GIU pin 0: nk.exe OAL ISR sub_9F002050 returns SYSINTR 39 on GIUINT bit0. */
        emu_.Get<Vr41xxGiu>().SetPinLevel(0, pending);
    }
};

}  /* namespace */

REGISTER_SERVICE(Vr4102HardModemUart);

}
