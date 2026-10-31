#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/host_widget_registry.h>
#include "../../peripherals/serial/serial_cradle.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../peripherals/nec_vrc4172/vrc4172_pcs.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "nec_mobilepro_700_family_id.h"

#include <cstdint>
#include <memory>

namespace cerfcore {

namespace {

constexpr uint32_t kModemChipSelect = 2u;
constexpr uint32_t kOpenBus         = 0xFFFFFFFFu;

/* MobilePro 700 built-in modem: off-chip stride-1 16550 at ISA-IO 0x17100000,
   driven by serial.dll's "Drivers\BuiltIn\HardModem" PDD (window map sub_1587374). */
class Vr4102HardModemUart : public Uart16550 {
public:
    /* The PDD opens the port with MCR |= OUT2 (sub_158702C) and never clears it, the
       PC/AT wiring in which OUT2 enables the part's IRQ buffer onto the bus. It enables
       IER 0x0D, within the PC16550D's 0x0F. */
    explicit Vr4102HardModemUart(CerfEmulator& emu)
        : Uart16550(emu, Config{/*ier_mask=*/0x0Fu,
                                /*irq_gate_mcr=*/0x08u,
                                /*irq_gate_ier=*/0u}) {}

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecMobilepro700;
    }

    uint32_t MmioBase() const override { return 0x17100000u; }
    uint32_t MmioSize() const override { return 0x08u; }   /* 8 regs, stride 1 */

    uint32_t ReadWord(uint32_t addr) override {
        if (!pcs_->Selects(kModemChipSelect, addr)) return kOpenBus;
        return Uart16550::ReadWord(addr);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        if (!pcs_->Selects(kModemChipSelect, addr)) return;
        Uart16550::WriteWord(addr, value);
    }

    /* The modem IC drives all four status lines into the UART's own MSR: HWGetModemStatus
       sub_15866A0 reads dword_158A678 (= base+6, MSR) and tests 0x10/0x20/0x40/0x80 for
       CTS/DSR/RI/DCD. Nothing is routed to a GIU pin, so no board pin map is needed. */
    void OnReady() override {
        pcs_ = &emu_.Get<Vrc4172Pcs>();
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
    Vrc4172Pcs* pcs_ = nullptr;
    std::unique_ptr<SerialCradle> cradle_;
};

}  /* namespace */

REGISTER_SERVICE(Vr4102HardModemUart);

}
