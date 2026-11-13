#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "pr31x00_pins.h"
#include <cerfcore/core/callback_list.h>

#include <atomic>
#include <cstdint>
#include <functional>

namespace cerfcore {

class Pr31x00Intc;

/* Philips PR31x00 I/O Module, TMPR3911/3912 ch.9. Registers $180-$198. */
class Pr31x00Io : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return 0x10C00180u; }
    uint32_t MmioSize() const override { return 0x1Cu; }   /* $180-$19B */

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    /* A board part drives one multi-function I/O pin. Each edge latches into the
       interrupt module's MFIOPOSINT / MFIONEGINT status (§8.3.3, §8.3.4). */
    void DriveMfioInput(uint32_t pin, bool level);

    /* A board part drives one of the 7 general purpose I/O pins. A rising edge
       latches IOPOSINT[pin] and a falling edge IONEGINT[pin], both in Interrupt
       Status 5 (§8.3.5). */
    void DriveIoInput(uint32_t pin, bool level);

    using PinObserver = std::function<void(const Pr31x00Pins&)>;
    void RegisterMfioOutObserver(PinObserver cb);
    void RegisterIoOutObserver(PinObserver cb);

    uint8_t  ReadByte(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 IO ReadByte", addr, 0); }
    uint16_t ReadHalf(uint32_t addr) override { HaltUnsupportedAccess("PR31x00 IO ReadHalf", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("PR31x00 IO WriteByte", addr, v); }
    void WriteHalf(uint32_t addr, uint16_t v) override { HaltUnsupportedAccess("PR31x00 IO WriteHalf", addr, v); }

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    struct Regs {
        uint32_t io_ctl;
        uint32_t mfio_dout;
        uint32_t mfio_direc;
        uint32_t mfio_sel;
        uint32_t io_pd;
        uint32_t mfio_pd;
    };

    /* Reset values, §9.3.1-§9.3.7. IODIREC and MFIODIREC reset to 0 (all pins
       inputs); IODEBSEL, IODOUT and MFIODOUT reset undefined and are taken as 0. */
    static constexpr Regs kResetRegs = {0x00000000u, 0x00000000u, 0x00000000u,
                                        0xF20F0FFFu, 0x0000007Fu, 0xFAF03FFCu};

    Regs regs_         = kResetRegs;
    bool powered_down_ = false;

    Pr31x00Intc* intc_ = nullptr;

    std::atomic<uint32_t> mfio_din_{0};
    std::atomic<uint32_t> io_din_{0};

    uint32_t    MfioOutputs() const;
    uint32_t    IoOutputs() const;
    uint32_t    IoDout() const;
    Pr31x00Pins PinsOf(uint32_t dout, uint32_t outputs, uint32_t pd) const;
    Pr31x00Pins MfioPins() const;
    Pr31x00Pins IoPins() const;

    void SetPoweredDown(bool powered_down);
    void NotifyMfioOut();
    void NotifyIoOut();

    CallbackList<const Pr31x00Pins&> mfio_out_observers_;
    CallbackList<const Pr31x00Pins&> io_out_observers_;
};

}
