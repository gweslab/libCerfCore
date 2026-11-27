#include "msm8255_clock_reset.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(Msm8255ClockReset);

bool Msm8255ClockReset::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255ClockReset::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { held_.clear(); });
}

void Msm8255ClockReset::RegisterListener(uint32_t clock,
                                         std::function<void()> fn) {
    listeners_.push_back({clock, std::move(fn)});
}

void Msm8255ClockReset::ResetBlocks(uint32_t clock) {
    bool served = false;
    for (const Listener& listener : listeners_) {
        if (listener.clock == clock) {
            listener.fn();
            served = true;
        }
    }
    if (!served) {
        emu_.Get<Fatal>().Die(
            "msm8255 clock reset: clock %u drives no block this program "
            "resets", clock);
    }
}

bool Msm8255ClockReset::IsHeld(uint32_t clock) const {
    return std::find(held_.begin(), held_.end(), clock) != held_.end();
}

void Msm8255ClockReset::Pulse(uint32_t clock) {
    ResetBlocks(clock);
    held_.erase(std::remove(held_.begin(), held_.end(), clock), held_.end());
}

void Msm8255ClockReset::Assert(uint32_t clock) {
    if (IsHeld(clock)) return;
    ResetBlocks(clock);
    held_.push_back(clock);
}

void Msm8255ClockReset::Deassert(uint32_t clock) {
    held_.erase(std::remove(held_.begin(), held_.end(), clock), held_.end());
}

void Msm8255ClockReset::RequireReleasedHeld(uint32_t clock, uint32_t addr) const {
    if (!IsHeld(clock)) return;
    emu_.Get<Fatal>().Die(
        "msm8255 clock reset: access at 0x%08X reaches a block whose clock %u "
        "reset is held asserted", addr, clock);
}

void Msm8255ClockReset::SaveState(StateWriter& w) const {
    w.Write("clock_reset_held_count", static_cast<uint32_t>(held_.size()));
    w.WriteBytes("clock_reset_held", held_.data(), held_.size() * sizeof(uint32_t));
}

void Msm8255ClockReset::RestoreState(StateReader& r) {
    uint32_t count = 0u;
    r.Read("clock_reset_held_count", count);
    held_.resize(count);
    r.ReadBytes("clock_reset_held", held_.data(), held_.size() * sizeof(uint32_t));
}

}
