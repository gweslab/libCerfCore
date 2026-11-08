#include "vr4122_clock_state.h"

#include <cerfcore/boards/board_context.h>
#include "vr4122_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../cpu/mips/mips_core_clock.h"
#include "../vr41xx/vr41xx_clock_strap.h"

namespace cerfcore {

REGISTER_SERVICE(Vr4122ClockState);

namespace {

struct ClkselRow {
    uint16_t clksp;
    uint16_t vtdiv;
    uint16_t vtdiv_modes;
};

/* VR4122 data sheet U15585EJ1V0DS Table 1-1 p.11, PClock = CLKX / CLKSP x 98
   (NetBSD hpcmips vrbcu_vrip_getcpuclock); CLKSEL 111 RFU. */
constexpr ClkselRow kClksel[8] = {
    {23u, 3u, 0x0Du}, {20u, 3u, 0x0Du}, {18u, 3u, 0x0Du}, {14u, 4u, 0x1Du},
    {12u, 5u, 0x39u}, {11u, 5u, 0x38u}, {10u, 6u, 0x78u}, {0u, 0u, 0x00u},
};

constexpr uint8_t kEcReserved = 0xFFu;
constexpr uint8_t kConfigEcOfVtdiv[8] = {kEcReserved, kEcReserved, 1u, 3u, 4u, 5u, 6u, kEcReserved};

constexpr bool StrapRowsHaveConfigEc() {
    for (const ClkselRow& r : kClksel) {
        if (r.clksp != 0u && (r.vtdiv >= 8u || kConfigEcOfVtdiv[r.vtdiv] == kEcReserved)) return false;
    }
    return true;
}
static_assert(StrapRowsHaveConfigEc());

constexpr uint64_t kPClockMult = 98u;
constexpr uint16_t kTclkDivWritable = 0x0107u;
constexpr uint16_t kVtDivMask = 0x7u;
constexpr uint16_t kTDivShift = 8u;
constexpr uint64_t kTClockPerMasterOut = 4u;

}

bool Vr4122ClockState::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Vr4122;
}

void Vr4122ClockState::OnReady() {
    Vr41xxClockState::OnReady();
    const ClkselRow& row = emu_.Get<Vr41xxClockStrap>().StrapRow(kClksel);
    clksp_       = row.clksp;
    strap_vtdiv_ = row.vtdiv;
    vtdiv_modes_ = row.vtdiv_modes;
}

const char* Vr4122ClockState::RejectedPending(uint16_t value) const {
    if ((value & ~kTclkDivWritable) != 0u) return "PMUTCLKDIVREG WriteHalf sets a write-0 RFU bit";
    if (((vtdiv_modes_ >> (value & kVtDivMask)) & 1u) == 0u) {
        return "PMUTCLKDIVREG WriteHalf with a VTDIV this CLKSEL does not allow";
    }
    return nullptr;
}

uint16_t Vr4122ClockState::VtDivMode() const {
    const uint16_t vtdiv = Active() & kVtDivMask;
    return vtdiv != 0u ? vtdiv : strap_vtdiv_;
}

uint16_t Vr4122ClockState::TDivMode() const {
    if ((Active() & kVtDivMask) == 0u) return 0u;
    return static_cast<uint16_t>((Active() >> kTDivShift) & 1u);
}

uint16_t Vr4122ClockState::ClkSpeedReg() const {
    return static_cast<uint16_t>((TDivMode() << 12) | (VtDivMode() << 8) | clksp_);
}

GuestCycleClock::Rate Vr4122ClockState::ResetRate() const {
    return GuestCycleClock::Rate{kVr41xxClkxHz * kPClockMult, clksp_};
}

uint64_t Vr4122ClockState::CyclesPerCountTick() const {
    const uint64_t tclk_per_vtclk = TDivMode() != 0u ? 4u : 2u;
    return uint64_t{VtDivMode()} * tclk_per_vtclk * kTClockPerMasterOut;
}

/* "The countdown uses a VTClock cycle" (VR4131 UM U15350EJ2V0UM 13.2.8 p253). */
uint64_t Vr4122ClockState::CyclesPerTclkCounterTick() const {
    return VtDivMode();
}

uint32_t Vr4122ClockState::ConfigEc() const { return kConfigEcOfVtdiv[strap_vtdiv_]; }

void Vr4122ClockState::ActiveRestored() { emu_.Get<MipsCoreClock>().CountRateRestored(); }

}
