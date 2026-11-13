#include "sh77xx_standby_entry.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "sh77xx_cpg.h"
#include "sh77xx_frqcr.h"

#include <utility>

namespace cerfcore {

REGISTER_SERVICE(Sh77xxStandbyEntry);

bool Sh77xxStandbyEntry::ShouldRegister() { return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kSh; }

void Sh77xxStandbyEntry::OnReady() {
    emu_.Get<GuestDeepSleep>().RegisterSleepEntryListener([this] { Enter(); });
    Sh77xxFrqcr* frqcr = emu_.TryGet<Sh77xxFrqcr>();
    if (frqcr == nullptr) return;
    Sh77xxCpg& cpg = emu_.Get<Sh77xxCpg>();
    frqcr->RegisterChangeListener([this, &cpg](uint16_t from, uint16_t to) {
        if (cpg.Pll1Multiplier(from) != cpg.Pll1Multiplier(to)) Enter();
    });
}

void Sh77xxStandbyEntry::RegisterListener(std::function<void()> listener) { listeners_.Add(std::move(listener)); }

void Sh77xxStandbyEntry::Enter() {
    listeners_.Call();
}

}
