#include "msm8255_clock_enables.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

REGISTER_SERVICE(Msm8255ClockEnables);

bool Msm8255ClockEnables::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255ClockEnables::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        for (auto& word : on_) word.store(0u, std::memory_order_release);
    });
}

void Msm8255ClockEnables::Record(uint32_t clock, bool on) {
    const uint32_t bit = 1u << (clock % 32u);
    const uint32_t old = on ? on_[clock / 32u].fetch_or(bit, std::memory_order_acq_rel)
                            : on_[clock / 32u].fetch_and(~bit, std::memory_order_acq_rel);
    if (((old & bit) != 0u) == on) return;
    for (const Watch& watch : watches_) {
        if (watch.clock == clock) watch.listener(on);
    }
}

void Msm8255ClockEnables::AddListener(uint32_t clock, Listener listener) {
    if (clock >= kMsm8255ClockIdCount) {
        emu_.Get<Fatal>().Die("msm8255 clock enables: a listener names clock %u, past the %u ids",
                              clock, kMsm8255ClockIdCount);
    }
    watches_.push_back({clock, std::move(listener)});
}

bool Msm8255ClockEnables::Enabled(uint32_t clock) const {
    return (on_[clock / 32u].load(std::memory_order_acquire) & (1u << (clock % 32u))) != 0u;
}

void Msm8255ClockEnables::RequireRunning(uint32_t clock, const char* block,
                                         const char* action) const {
    if (!Enabled(clock)) {
        emu_.Get<Fatal>().Die("msm8255 %s: clock %u is off while the block %s", block, clock,
                              action);
    }
}

void Msm8255ClockEnables::SaveState(StateWriter& w) const {
    for (const auto& word : on_)
        w.Write<uint32_t>("clock_on", word.load(std::memory_order_acquire));
}

void Msm8255ClockEnables::RestoreState(StateReader& r) {
    for (auto& word : on_) {
        uint32_t bits = 0u;
        r.Read("clock_on", bits);
        word.store(bits, std::memory_order_release);
    }
}

}
