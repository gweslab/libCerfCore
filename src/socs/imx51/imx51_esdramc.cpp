#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "imx51_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "imx51_src.h"

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

/* i.MX51 Enhanced SDRAM Controller (ESDRAMC), MCIMX51RM Rev.1 Ch.30, register
   base 0x83FD9000. The DDR timing/delay/mode registers have no effect in CERF
   (guest DRAM is already host-backed), so they are pure R/W storage - except
   the ESDSCR config handshake below. */
constexpr uint32_t kBase = 0x83FD9000u;
constexpr uint32_t kSize = 0x00001000u;

constexpr uint32_t kEsdscrOff = 0x14u;       /* ESDSCR, 0x83FD9014 (Table 30-4) */
constexpr uint32_t kConReq    = 1u << 15;    /* ESDSCR[15] CON_REQ, R/W */
constexpr uint32_t kConAck    = 1u << 14;    /* ESDSCR[14] CON_ACK, RO  */

constexpr uint32_t kEsdgprOff         = 0x34u;
constexpr uint32_t kEsdgprUndefined   = 0x000000FFu;

struct EsdramcReset { uint32_t off; uint32_t val; };
constexpr EsdramcReset kResets[] = {
    {0x00u, 0x01110000u}, {0x04u, 0xE99668BAu}, {0x08u, 0x01120000u}, {0x0Cu, 0xE99A64A7u},
    {0x10u, 0x000A4680u}, {0x14u, 0x00008000u}, {0x20u, 0x00F48000u}, {0x24u, 0x00F48000u},
    {0x28u, 0x00F48000u}, {0x2Cu, 0x00F48000u}, {0x30u, 0x00F48000u}, {0x34u, 0x20020000u},
};

class Imx51Esdramc : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx51;
    }
    void OnReady() override {
        ResetRegisters();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (emu_.Get<Imx51Src>().WarmResetKeepsConfiguration(kind)) return;
            ResetRegisters();
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kBase;
        uint32_t v = regs_[off >> 2];
        if (off == kEsdscrOff) {
            /* ESDSCR[14] CON_ACK (RO, Table 30-6): the controller acks the
               config-access request once idle with no AXI pending. CERF issues
               no DDR AXI traffic, so it is always idle and acks immediately,
               i.e. CON_ACK tracks CON_REQ - the OAL polls (CON_REQ & CON_ACK). */
            v = (v & ~kConAck) | ((v & kConReq) ? kConAck : 0u);
        }
        return v;
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kBase;
        regs_[off >> 2] = (off == kEsdscrOff) ? (value & ~kConAck) : value;
    }

    /* JIT-thread-only register file (no worker thread). */
    void SaveState(StateWriter& w) override    { w.WriteBytes("regs", regs_.data(), sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_.data(), sizeof(regs_)); }

private:
    void ResetRegisters() {
        const uint32_t gpr_undefined = regs_[kEsdgprOff >> 2] & kEsdgprUndefined;
        regs_.fill(0u);
        for (const auto& r : kResets) regs_[r.off >> 2] = r.val;
        regs_[kEsdgprOff >> 2] |= gpr_undefined;
    }

    std::array<uint32_t, kSize / 4> regs_{};
};

}  /* namespace */

REGISTER_SERVICE(Imx51Esdramc);

}
