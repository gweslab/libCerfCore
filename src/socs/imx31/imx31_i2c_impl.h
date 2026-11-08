#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "imx31_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../freescale_i2c_regs.h"
#include "../freescale_module_clocks.h"

#include <cstdint>

namespace cerfcore {

namespace cerf_imx31_i2c_detail {

using namespace cerf_freescale_i2c;

template <uint32_t kBase, FreescaleModule kModule>
class Imx31I2cImpl : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            iadr_ = ifdr_ = i2cr_ = i2dr_ = 0;
            i2sr_ = kI2srReset;
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
            case kOffIadr: return iadr_;
            case kOffIfdr: return ifdr_;
            case kOffI2cr: return i2cr_;
            case kOffI2sr: return i2sr_;
            case kOffI2dr:
                module_clocks_->RequireRunning(kModule, "receives a byte");
                i2sr_ |= kI2srIcf;
                return i2dr_;
        }
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
            case kOffIadr: iadr_ = value; return;
            case kOffIfdr: ifdr_ = value; return;
            case kOffI2cr: {
                const bool was_master = (i2cr_ & kI2crMsta) != 0;
                const bool is_master  = (value & kI2crMsta) != 0;
                if (was_master != is_master)
                    module_clocks_->RequireRunning(kModule, "drives a START or STOP");
                i2cr_ = static_cast<uint16_t>(value & kI2crReadable);
                if (!was_master && is_master)      i2sr_ |= kI2srIbb;
                else if (was_master && !is_master) i2sr_ &= ~(kI2srIbb | kI2srIcf);
                return;
            }
            /* IIF/IAL are write-0-to-clear (Table 26-4). */
            case kOffI2sr: i2sr_ &= (value | ~(kI2srIif | kI2srIal)); return;
            /* Audio careful stub (§0.3): accept the codec byte, complete the
               transfer instantly with ACK so the polling driver proceeds. */
            case kOffI2dr:
                module_clocks_->RequireRunning(kModule, "transmits a byte");
                i2dr_ = value;
                i2sr_ |= (kI2srIcf | kI2srIif);
                i2sr_ &= ~kI2srRxak;
                return;
        }
        HaltUnsupportedAccess("WriteHalf", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write("iadr", iadr_); w.Write("ifdr", ifdr_); w.Write("i2cr_readable", i2cr_);
        w.Write("i2sr", i2sr_); w.Write("i2dr", i2dr_);
    }
    void RestoreState(StateReader& r) override {
        r.Read("iadr", iadr_); r.Read("ifdr", ifdr_); r.Read("i2cr_readable", i2cr_);
        r.Read("i2sr", i2sr_); r.Read("i2dr", i2dr_);
    }

private:
    uint16_t iadr_ = 0, ifdr_ = 0, i2cr_ = 0, i2sr_ = kI2srReset, i2dr_ = 0;
    FreescaleModuleClocks* module_clocks_ = nullptr;
};

}  /* namespace cerf_imx31_i2c_detail */

}
