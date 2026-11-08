#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "askey_pc3xx_family_id.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../socs/pxa255/pxa255_gpio.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <cstring>

namespace cerfcore {

namespace {

/* Falcon (Askey PC320/PC331) board CPLD at PA 0xA3CC3000 (carved out of DRAM so
   it routes here). nk.exe OEMIoControl 0x101003C (IOCTL_HAL_REBOOT, sub_800F5E20)
   writes 3 to PA 0xA3CC380C (offset 0x80C) to reset the board - trigger the warm
   reset on that write, else the post-reboot OAL spins and WarmCheck never signals. */
class AskeyPc3xxBoardCpld : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::AskeyPc3xx;
    }
    void OnReady() override {
        auto& reset = emu_.Get<GuestCpuReset>();
        reset.RegisterResetListener([this](ResetLineKind kind) {
            power_on_ = kind == ResetLineKind::Rtc;
            if (power_on_) std::memset(regs_, 0, sizeof(regs_));
        });
        reset.RegisterResetReleaseListener([this, &reset] {
            if (reset.DeliveredResetReportedWarm()) SetRamValid(true);
            else if (power_on_) SetRamValid(false);
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0xA3CC3000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint8_t  ReadByte (uint32_t addr) override { return regs_[addr - MmioBase()]; }
    uint32_t ReadWord (uint32_t addr) override { return cerf::le::U32(regs_, addr - MmioBase()); }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - MmioBase();
        regs_[off] = value;
        if (off == kReboot && value == kRebootCmd) TriggerReset();
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        cerf::le::Put32(regs_ + off, value);
        if (off == kReboot && (value & 0xFFu) == kRebootCmd) TriggerReset();
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }

private:
    static constexpr uint32_t kReboot    = 0x80Cu;  /* PA 0xA3CC380C - base 0xA3CC3000. */
    static constexpr uint32_t kRebootCmd = 3u;

    void TriggerReset() {
        LOG(SocReset, "Falcon CPLD PA 0xA3CC380C=3: board reboot\n");
        SetRamValid(true);
        emu_.Get<GuestCpuReset>().WarmReset();
    }

    void SetRamValid(bool valid) {
        auto& gpio = emu_.Get<Pxa255Gpio>();
        gpio.SetInputLevel(2, valid);
        gpio.SetInputLevel(3, valid);
    }

    bool power_on_ = false;

    /* +4 tail so a word access at the last in-range offset (0xFFF) cannot
       overrun; the guest only addresses off < 0x1000 (dispatcher contract). */
    uint8_t regs_[0x1000 + 4] = {};
};

}  /* namespace */

REGISTER_SERVICE(AskeyPc3xxBoardCpld);

}
