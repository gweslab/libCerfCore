#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "imx31_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

/* i.MX31 IOMUXC pin-mux config (MCIMX31RM Ch 4 Table 4-1: GPR @0x008,
   SW_MUX_CTL @0x00C-0x150, SW_PAD_CTL @0x154-0x308, all R/W). Pure R/W storage
   - CERF models no physical pads, so mux values select nothing here; software
   just writes and reads them back. */
constexpr uint32_t kBase = 0x43FAC000u;
constexpr uint32_t kSize = 0x0000030Cu;  /* through SW_PAD_CTL last reg 0x308 (Table 4-1) */
constexpr uint32_t kFirstRegister = 0x008u;

constexpr uint32_t kResetValues[] = {
    0x00000000u, 0x00000012u, 0x01121200u, 0x01010101u, 0x01010101u, 0x00010101u,
    0x00000000u, 0x01000000u, 0x01010101u, 0x00000001u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u,
    0x00000000u, 0x01011200u, 0x12121212u, 0x12121212u, 0x01010101u, 0x00000000u,
    0x01010101u, 0x00000000u, 0x01000001u, 0x01010101u, 0x01010101u, 0x01010101u,
    0x00000101u, 0x00000000u, 0x00000000u, 0x00000000u, 0x01010101u, 0x01010000u,
    0x01010000u, 0x01010000u, 0x01010000u, 0x01010101u, 0x01010101u, 0x01010101u,
    0x12120101u, 0x12001212u, 0x00000000u, 0x12000000u, 0x12121212u, 0x12121212u,
    0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u,
    0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u,
    0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u,
    0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u,
    0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u, 0x12121212u,
    0x01010112u, 0x01010101u, 0x01120101u, 0x01010101u, 0x01010001u, 0x0A000000u,
    0x1A06C1A0u, 0x081285A0u, 0x1A0681A0u, 0x0A0281A0u, 0x0A128481u, 0x0A1284A1u,
    0x0A328CA3u, 0x0A368CA3u, 0x0A328CA3u, 0x0A328CA3u, 0x0A328CA3u, 0x1A328CA3u,
    0x1A368DA3u, 0x1A368DA3u, 0x1A368DA3u, 0x1A368DA3u, 0x1A368DA3u, 0x1A368DA3u,
    0x1A0681A0u, 0x1B0681A0u, 0x1A0681A0u, 0x1A0681A0u, 0x1A0681A0u, 0x1B0681A0u,
    0x1A0681A0u, 0x1A0681A0u, 0x1B06C0A3u, 0x1A020D90u, 0x1A0681A0u, 0x1A0681A0u,
    0x1A0681A0u, 0x1A0681A0u, 0x1A0681A0u, 0x1A0683A8u, 0x1A0681A0u, 0x1A06C1A0u,
    0x1A0681B0u, 0x1A0681A0u, 0x1A0681A0u, 0x1B06C1A0u, 0x1B0681A0u, 0x1B0681B0u,
    0x1A0681A0u, 0x1A0681A0u, 0x1A0681B0u, 0x1A06C1A0u, 0x1B0681A0u, 0x1A0681A0u,
    0x1A0681B0u, 0x1336C1B0u, 0x12348D23u, 0x12348D23u, 0x12348D23u, 0x12348D23u,
    0x12348D23u, 0x0A2281A0u, 0x1A2689A2u, 0x1A2689A2u, 0x1A2609A2u, 0x1A2689A2u,
    0x12749D27u, 0x12749D27u, 0x12749D27u, 0x12749D27u, 0x12749D27u, 0x1A368D27u,
    0x1A368DA3u, 0x18160DA3u, 0x18160581u, 0x121A9DA0u, 0x12748521u, 0x2A328D27u,
    0x0A368CA3u, 0x12749CA3u, 0x0A328CA3u, 0x12728CA3u, 0x12749D27u, 0x12749D27u,
    0x12749D27u, 0x12749D27u, 0x12749D27u, 0x12749D27u, 0x12749D27u, 0x12749D27u,
    0x12749D27u, 0x12749D27u, 0x12749D27u, 0x0A149D27u, 0x0A729CA1u, 0x0A729CA7u,
    0x0A729CA7u, 0x0A729CA7u, 0x0A729CA7u, 0x0A729CA7u, 0x0A729CA7u, 0x0A729CA7u,
    0x0A729CA7u, 0x0A0280A7u, 0x0A0280A0u, 0x0A02C180u, 0x0A0280A0u, 0x1B029CA0u,
    0x1A02C1B0u, 0x1A0E81A0u, 0x1A0681A0u, 0x1A0681A0u, 0x1A0681A0u, 0x0B0681A0u,
    0x1A0681A0u,
};
static_assert(sizeof(kResetValues) == kSize - kFirstRegister);

class Imx31Iomuxc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetRegisters(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override { return regs_[(addr - kBase) >> 2]; }
    void WriteWord(uint32_t addr, uint32_t value) override {
        regs_[(addr - kBase) >> 2] = value;
    }

    /* JIT-thread-only register file (no worker thread). */
    void SaveState(StateWriter& w) override    { w.WriteBytes("regs", regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_.data(), sizeof(regs_)); }

private:
    void ResetRegisters() {
        for (uint32_t i = 0; i < sizeof(kResetValues) / 4u; ++i) regs_[kFirstRegister / 4u + i] = kResetValues[i];
    }

    std::array<uint32_t, kSize / 4> regs_{};
};

}  /* namespace */

REGISTER_SERVICE(Imx31Iomuxc);

}
