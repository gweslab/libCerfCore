#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "pxa255_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

class Pxa255LcdController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Pxa255;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x44000000u; }
    uint32_t MmioSize() const override { return 0x00001000u; }

    uint32_t ReadWord (uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    static constexpr uint32_t kLccr0   = 0x000u;  /* §7.6.1: ENB = bit 0. */
    static constexpr uint32_t kLastReg = 0x21Cu;  /* LDCMD1, end of decoded window. */

    void ResetRegisters() {
        for (uint32_t off : {0x000u, 0x004u, 0x008u, 0x00Cu, 0x020u, 0x024u, 0x038u, 0x20Cu, 0x21Cu})
            regs_[off / 4u] = 0u;
        regs_[0x040u / 4u] = 0x00AA5500u;
        regs_[0x044u / 4u] = 0x0000754Fu;
    }

    uint32_t regs_[(kLastReg / 4u) + 1u] = {};
};

uint32_t Pxa255LcdController::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off <= kLastReg && (off & 3u) == 0u) return regs_[off / 4u];
    HaltUnsupportedAccess("ReadWord", addr, 0);
}

void Pxa255LcdController::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off == kLccr0 && (value & 0x1u))
        HaltUnsupportedAccess("WriteWord(LCCR0.ENB: PXA255 internal LCD scanout not modeled)",
                              addr, value);
    if (off <= kLastReg && (off & 3u) == 0u) { regs_[off / 4u] = value; return; }
    HaltUnsupportedAccess("WriteWord", addr, value);
}

void Pxa255LcdController::SaveState(StateWriter& w) {
    w.WriteBytes("regs", regs_, sizeof(regs_));
}

void Pxa255LcdController::RestoreState(StateReader& r) {
    r.ReadBytes("regs", regs_, sizeof(regs_));
}

}  /* namespace */

REGISTER_SERVICE(Pxa255LcdController);

}
