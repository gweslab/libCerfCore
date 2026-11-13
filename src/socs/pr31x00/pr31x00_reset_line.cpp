#include "pr31x00_reset_line.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <utility>

namespace cerfcore {

REGISTER_SERVICE(Pr31x00ResetLine);

bool Pr31x00ResetLine::ShouldRegister() {
    const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00ResetLine::OnReady() {
    reset_ = &emu_.Get<GuestCpuReset>();
    reset_->RegisterResetListener([this](ResetLineKind kind) {
        if (kind != ResetLineKind::Rtc) return;
        reset_listeners_.Call();
    });
    reset_->RegisterResetReleaseListener([this] {
        if (reset_->DeliveringKind() != ResetLineKind::Rtc) return;
        release_listeners_.Call();
    });
}

void Pr31x00ResetLine::RegisterResetListener(std::function<void()> fn) {
    reset_listeners_.Add(std::move(fn));
}

void Pr31x00ResetLine::RegisterReleaseListener(std::function<void()> fn) {
    release_listeners_.Add(std::move(fn));
}

bool Pr31x00ResetLine::Resetting() const {
    return reset_->Delivering() && reset_->DeliveringKind() == ResetLineKind::Rtc;
}

}
