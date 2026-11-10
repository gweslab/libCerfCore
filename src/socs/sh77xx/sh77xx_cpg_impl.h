#pragma once

#include <cstdint>
#include <iterator>
#include <string_view>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../cpu/sh/sh_board_clock.h"
#include "sh77xx_cpg.h"

namespace cerfcore {

struct Sh77xxFrqcrSetting {
    uint16_t frqcr;
    uint8_t  cpu_per_ckio;
    uint8_t  ckio_per_pclk;
    uint8_t  pll1;
};

struct Sh77xxCpgModel {
    const Sh77xxFrqcrSetting* modes34;
    uint32_t                  modes34_count;
};

inline constexpr uint16_t kSh77xxFrqcrReset = 0x0102u;

template <const std::string_view& Soc, const Sh77xxCpgModel& M>
class Sh77xxCpgBase : public Sh77xxCpg {
public:
    using Sh77xxCpg::Sh77xxCpg;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == Soc;
    }

    GuestCycleClock::Rate ResetCpuRate() const override { return CpuRate(kSh77xxFrqcrReset); }

    GuestCycleClock::Rate CpuRate(uint16_t frqcr) const override {
        return GuestCycleClock::Rate{emu_.Get<ShBoardClock>().CkioHz() * Setting(frqcr).cpu_per_ckio, 1u};
    }

    GuestCycleClock::Rate PeripheralRate(uint16_t frqcr) const override {
        return GuestCycleClock::Rate{emu_.Get<ShBoardClock>().CkioHz(), Setting(frqcr).ckio_per_pclk};
    }

    uint8_t Pll1Multiplier(uint16_t frqcr) const override { return Setting(frqcr).pll1; }

private:
    const Sh77xxFrqcrSetting& Setting(uint16_t frqcr) const {
        if (!emu_.Get<ShBoardClock>().Pll1Switchable()) {
            emu_.Get<Fatal>().Die("Sh77xxCpg: FRQCR 0x%04X in clock mode 0, 1, 2 or 7", frqcr);
        }
        for (uint32_t i = 0; i < M.modes34_count; ++i) {
            if (M.modes34[i].frqcr == frqcr) return M.modes34[i];
        }
        emu_.Get<Fatal>().Die("Sh77xxCpg: FRQCR 0x%04X is not a clock mode 3/4 setting", frqcr);
    }
};

}
