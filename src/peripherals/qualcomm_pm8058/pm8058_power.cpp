#include "pm8058_power.h"

#include "../../core/board_parts.h"
#include "qualcomm_pm8058_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

bool Pm8058Power::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::QualcommPm8058);
}

void Pm8058Power::OnReady() {
    auto& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterPowerOnWake();
    sleep.RegisterParkWakeSource([this] {
        return Off() && turn_on_.load(std::memory_order_acquire);
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        turn_on_.store(false, std::memory_order_release);
        if (off_.exchange(false, std::memory_order_acq_rel)) {
            LOG(Periph, "[PM8058] turn-on\n");
        }
    });
}

void Pm8058Power::PowerOff() {
    off_.store(true, std::memory_order_release);
    LOG(Periph, "[PM8058] power off\n");
    power_off_listeners_.Call();
    emu_.Get<GuestDeepSleep>().Enter();
}

void Pm8058Power::AddPowerOffListener(std::function<void()> fn) {
    power_off_listeners_.Add(std::move(fn));
}

void Pm8058Power::Restart() {
    turn_on_.store(true, std::memory_order_release);
    PowerOff();
}

void Pm8058Power::RequestTurnOn() {
    if (!Off()) return;
    turn_on_.store(true, std::memory_order_release);
    LOG(Periph, "[PM8058] turn-on trigger\n");
}

void Pm8058Power::SaveState(StateWriter& w) {
    w.Write<uint8_t>("pm8058_off", Off() ? 1u : 0u);
    w.Write<uint8_t>("pm8058_turn_on", turn_on_.load(std::memory_order_acquire) ? 1u : 0u);
}

void Pm8058Power::RestoreState(StateReader& r) {
    uint8_t off = 0u, turn_on = 0u;
    r.Read("pm8058_off", off);
    r.Read("pm8058_turn_on", turn_on);
    off_.store(off != 0u, std::memory_order_release);
    turn_on_.store(turn_on != 0u, std::memory_order_release);
}

REGISTER_SERVICE(Pm8058Power);

}
