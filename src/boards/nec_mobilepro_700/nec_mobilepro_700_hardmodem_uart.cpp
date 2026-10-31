#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/serial/serial_cradle.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../peripherals/nec_vrc4172/vrc4172_chip_selected.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>
#include <memory>

namespace cerfcore {

namespace {

constexpr uint32_t kModemChipSelect = 2u;

class Vr4102HardModemUart : public Vrc4172ChipSelected<Uart16550> {
public:
    explicit Vr4102HardModemUart(CerfEmulator& emu)
        : Vrc4172ChipSelected<Uart16550>(emu, Config{0x0Fu, 0x08u, 0u}) {}

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    uint32_t MmioBase() const override { return 0x17100000u; }
    uint32_t MmioSize() const override { return 0x08u; }   /* 8 regs, stride 1 */

    void OnChipSelectReady() override {
        Uart16550::OnReady();

        cradle_ = std::make_unique<SerialCradle>(emu_, *this, "COM4 (internal modem)");
        SetActivityWidget(cradle_.get());
        emu_.Get<HostWidgetRegistry>().Register(cradle_.get());
        LOG(Periph, "[UART] HardModem attached as serial endpoint line 'COM4'\n");
    }

    void OnShutdown() override {
        if (cradle_) cradle_->OnShutdown();
    }

protected:
    uint32_t    ChipSelect() const override { return kModemChipSelect; }
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "HardModem"; }

    void SetInterruptLine(bool pending) override {
        /* GIU pin 0: nk.exe OAL ISR sub_9F002050 returns SYSINTR 39 on GIUINT bit0. */
        emu_.Get<Vr41xxGiu>().SetPinLevel(0, pending);
    }

    void SaveState(StateWriter& w) override {
        Uart16550::SaveState(w);
        if (cradle_) cradle_->SaveCradleState(w);
    }
    void RestoreState(StateReader& r) override {
        Uart16550::RestoreState(r);
        if (cradle_) cradle_->RestoreCradleState(r);
    }
    void PostRestore() override {
        Uart16550::PostRestore();
        if (cradle_) cradle_->PostRestore();
    }

private:
    std::unique_ptr<SerialCradle> cradle_;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102HardModemUart);

}
