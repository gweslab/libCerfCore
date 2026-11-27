#include "casio_cassiopeia_a10_on_key.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../core/deferred_jobs.h"
#include "../../socs/hd6417096/hd6417096_on_key_input.h"
#include "casio_cassiopeia_a10_family_id.h"
#include "casio_cassiopeia_a10_nmi_line.h"

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(CasioCassiopeiaA10OnKey);

namespace {

constexpr int64_t kButtonPressNs = 200'000'000;

}

bool CasioCassiopeiaA10OnKey::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioCassiopeiaA10;
}

void CasioCassiopeiaA10OnKey::OnReady() {
    on_key_input_ = &emu_.Get<Hd6417096OnKeyInput>();
    nmi_line_     = &emu_.Get<CasioCassiopeiaA10NmiLine>();
    clock_        = &emu_.Get<GuestCycleClock>();
    release_      = clock_->Add([this] { EndButtonPress(); });
    clock_->RegisterRateListener([this] { ArmRelease(); });
    GuestDeepSleep& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterUserWakeInput([this] {
        Hold(false);
        Release();
    });
    sleep.RegisterSleepEntryListener([this] { standby_.store(true, std::memory_order_release); });
    sleep.RegisterSleepListener([this](bool asleep) {
        if (!asleep) standby_.store(false, std::memory_order_release);
    });
}

void CasioCassiopeiaA10OnKey::Press() {
    if (standby_.load(std::memory_order_acquire)) {
        emu_.Get<GuestDeepSleep>().Resume();
        return;
    }
    emu_.Get<DeferredJobs>().PostToGuestThread([this] { StartButtonPress(); });
}

void CasioCassiopeiaA10OnKey::SetHeld(bool down) {
    button_press_ = false;
    clock_->Disarm(release_);
    if (down && standby_.load(std::memory_order_acquire)) {
        held_ = false;
        emu_.Get<DeferredJobs>().Post([this] { emu_.Get<GuestDeepSleep>().Resume(); });
        return;
    }
    held_ = down;
    if (down) {
        Hold(true);
        return;
    }
    Release();
}

void CasioCassiopeiaA10OnKey::StartButtonPress() {
    if (held_) return;
    button_press_  = true;
    release_at_ns_ = clock_->NowNs() + kButtonPressNs;
    Hold(true);
    ArmRelease();
}

void CasioCassiopeiaA10OnKey::EndButtonPress() {
    if (!button_press_) return;
    button_press_ = false;
    Release();
}

void CasioCassiopeiaA10OnKey::ArmRelease() {
    if (!button_press_) return;
    clock_->Arm(release_, clock_->CycleAtNs(release_at_ns_));
}

void CasioCassiopeiaA10OnKey::Hold(bool cpu_running) {
    on_key_input_->SetOnKeyHeld(true, cpu_running);
    nmi_line_->SetOnKey(true);
}

void CasioCassiopeiaA10OnKey::Release() {
    on_key_input_->SetOnKeyHeld(false, false);
    nmi_line_->SetOnKey(false);
}

}
