#pragma once

#include <cstdint>
#include <string_view>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../cpu/mips/mips_core_clock.h"
#include "vr41xx_clock_strap.h"

namespace cerfcore {

struct Vr41xxClkselRow {
    uint16_t clksp;
    uint16_t pclock_per_masterout;
    uint16_t pmu_div_modes;
};

inline constexpr uint8_t kVr41xxEcReserved = 0xFFu;

struct Vr41xxCoreClockModel {
    uint64_t        pclock_mult;
    uint64_t        tclock_per_masterout;
    Vr41xxClkselRow rows[8];
    uint8_t         config_ec_of_ratio[8];
};

constexpr uint64_t Vr41xxPclockPerTclock(const Vr41xxCoreClockModel& m, const Vr41xxClkselRow& r) {
    return r.pclock_per_masterout / m.tclock_per_masterout;
}

constexpr bool Vr41xxRowsHoldWholeTClocks(const Vr41xxCoreClockModel& m) {
    for (const Vr41xxClkselRow& r : m.rows) {
        if (r.pclock_per_masterout % m.tclock_per_masterout != 0u) return false;
    }
    return true;
}

constexpr bool Vr41xxRowsHaveConfigEc(const Vr41xxCoreClockModel& m) {
    for (const Vr41xxClkselRow& r : m.rows) {
        if (r.clksp == 0u) continue;
        const uint64_t ratio = Vr41xxPclockPerTclock(m, r);
        if (ratio >= 8u || m.config_ec_of_ratio[ratio] == kVr41xxEcReserved) return false;
    }
    return true;
}

template <const std::string_view& Soc, const Vr41xxCoreClockModel& M>
class Vr41xxCoreClockBase : public MipsCoreClock {
    static_assert(M.tclock_per_masterout != 0u && Vr41xxRowsHoldWholeTClocks(M));
    static_assert(Vr41xxRowsHaveConfigEc(M));

public:
    using MipsCoreClock::MipsCoreClock;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == Soc;
    }

    void OnReady() override { row_ = &Row(); }

    GuestCycleClock::Rate ResetRate() const override {
        return GuestCycleClock::Rate{kVr41xxClkxHz * M.pclock_mult, row_->clksp};
    }

    uint64_t CyclesPerCountTick() const override { return row_->pclock_per_masterout; }

    uint64_t CyclesPerTclkCounterTick() const override { return Vr41xxPclockPerTclock(M, *row_); }

    uint32_t ConfigEc() const override { return M.config_ec_of_ratio[Vr41xxPclockPerTclock(M, *row_)]; }

protected:
    virtual const Vr41xxClkselRow& Row() {
        return emu_.Get<Vr41xxClockStrap>().StrapRow(M.rows);
    }

private:
    const Vr41xxClkselRow* row_ = nullptr;
};

}
