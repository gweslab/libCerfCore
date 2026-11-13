#pragma once

#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "vr41xx_cmu.h"
#include "vr41xx_giu.h"
#include "vr41xx_icu.h"
#include "vr41xx_serial_wiring.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>

namespace cerfcore {

/* VR41xx on-chip SIU: NS16550-compatible UART at base 0x0C000000, byte-addressed (SCR at
   0x0C000007); interrupt is SYSINT1 D9 SIUINTR (VR4121 UM 15.2.1 p373 / VR4102 UM 14.2.1
   p295). Registers 0x00-0x08 chip-identical (VR4121 UM Table 25-1 p553 == VR4102 UM
   Table 24-1 p461); VR4102 stops at 0x08, VR4121 adds 0x09 SIURESET / 0x0A SIUCSEL. */
class Vr41xxSiu : public Uart16550 {
public:
    explicit Vr41xxSiu(CerfEmulator& emu)
        : Uart16550(emu, Config{.ier_mask = 0x0Fu, .irq_gate_mcr = 0u, .irq_gate_ier = 0u},
                    Clock{18432000u, 1u}) {}

    uint32_t MmioBase() const override { return 0x0C000000u; }
    uint32_t MmioSize() const override { return 0x20u; }   /* SIU block; HSP follows at +0x20 */

    void OnReady() override;

    void SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) override;
    void PushRx(const uint8_t* data, size_t n) override;

protected:
    void DeliverTx(uint8_t byte) override;
    bool ControlOutputsDriven() override;

    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "SIU"; }

    void SetInterruptLine(bool pending) override {
        emu_.Get<Vr41xxIcu>().SetSysint1Source(kSiuIntr, pending);
    }

    void OnLineReset(ResetLineKind kind) override;

    uint32_t ReadExtReg(uint32_t idx) override;
    void     WriteExtReg(uint32_t idx, uint32_t value) override;

    /* SIU registers at index >= 9. VR4102 has one undocumented register there; VR4121 has
       SIURESET (0x09) + SIUCSEL (0x0A). Default: unsupported access halts. */
    virtual uint32_t ReadChipExtReg(uint32_t idx)                { return Uart16550::ReadExtReg(idx); }
    virtual void     WriteChipExtReg(uint32_t idx, uint32_t val) { Uart16550::WriteExtReg(idx, val); }
    virtual void     ResetChip()                      = 0;
    virtual void     SaveChipState(StateWriter& w)    = 0;
    virtual void     RestoreChipState(StateReader& r) = 0;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    static constexpr uint16_t kSiuIntr = 1u << 9;   /* SYSINT1 D9 SIUINTR */
    static constexpr uint32_t kIrselIndex    = 8u;
    static constexpr uint8_t  kIrselWritable = 0x3Fu;
    static constexpr uint8_t  kIrselSirsel   = 0x01u;
    static constexpr uint16_t kSiuClocks     = kCmuMskSiu | kCmuMskSsiu;

    void TrackClock(uint16_t mask_bit);
    void RefreshClocks();
    void DriveCarrierPin(bool carrier);
    bool DcdLevel();
    bool SupplyOn();
    bool Rs232PinsLive();
    bool Rs232Selected();
    bool TransmitsOnTxd();
    void ApplyModemInputs();
    void ApplyModemInputsLocked();

    std::atomic<uint8_t>  irsel_{0u};
    std::atomic<uint16_t> clocks_{0u};
    std::atomic<bool>     hibernating_{false};

    std::mutex modem_mtx_;
    bool       connector_cts_ = false;
    bool       connector_dsr_ = false;

    Vr41xxSerialWiring*               wiring_  = nullptr;
    std::optional<Vr41xxSerialModem>  modem_;
};

}
