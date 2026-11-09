#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/host/keyboard_input.h>
#include <cerfcore/host/keyboard_map.h>
#include <cerfcore/host/keyboard_router.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../socs/vr41xx/vr41xx_pmu.h"
#include "compaq_aero_1500_family_id.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr int     kPowerButtonPin    = 8;
constexpr int     kOnChipPullPins[]  = {1, 7, 8, 9, 10, 11, 12};
constexpr int     kBoardPullUpPins[] = {16, 17, 18};
constexpr int64_t kWakeHoldNs        = 300000000;

class CompaqAero1500Buttons : public KeyboardInput {
public:
    using KeyboardInput::KeyboardInput;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    void OnReady() override {
        giu_          = &emu_.Get<Vr41xxGiu>();
        pmu_          = &emu_.Get<Vr41xxPmu>();
        clock_        = &emu_.Get<GuestCycleClock>();
        wake_release_ = clock_->Add([this] { SetPressed(kPowerButtonPin, false); });
        ReleaseAll();
        emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
            clock_->Disarm(wake_release_);
            wake_hold_.store(false, std::memory_order_release);
            ReleaseAll();
        });
        emu_.Get<GuestDeepSleep>().RegisterUserWakeInput([this] {
            wake_hold_.store(true, std::memory_order_release);
            SetPressed(kPowerButtonPin, true);
        });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (!wake_hold_.exchange(false, std::memory_order_acq_rel)) return;
            clock_->Arm(wake_release_, clock_->Cycles() + clock_->NsToCycles(kWakeHoldNs));
        });
        emu_.Get<KeyboardRouter>().Register(this);
    }

    std::string SourceName() const override { return "Aero buttons"; }

    void OnHostKey(uint8_t vk, bool key_up) override {
        uint32_t code = 0;
        if (!emu_.Get<KeyboardMap>().BaseDeviceCode(vk, code)) return;
        SetPressed(static_cast<int>(code), !key_up);
    }

private:
    static bool HasBoardPullUp(int pin) {
        for (int p : kBoardPullUpPins) {
            if (p == pin) return true;
        }
        return false;
    }

    void SetPressed(int pin, bool pressed) {
        if (pin == kPowerButtonPin) pmu_->SetPowerSwitch(pressed);
        if (HasBoardPullUp(pin)) {
            giu_->SetPinLevel(pin, !pressed);
            return;
        }
        if (pressed) giu_->SetPinLevel(pin, pin == kPowerButtonPin);
        else giu_->ReleasePin(pin);
    }

    void ReleaseAll() {
        for (int pin : kOnChipPullPins) SetPressed(pin, false);
        for (int pin : kBoardPullUpPins) SetPressed(pin, false);
    }

    Vr41xxGiu*              giu_          = nullptr;
    Vr41xxPmu*              pmu_          = nullptr;
    GuestCycleClock*        clock_        = nullptr;
    GuestCycleClock::Event* wake_release_ = nullptr;
    std::atomic<bool>       wake_hold_{false};
};

}

REGISTER_SERVICE(CompaqAero1500Buttons);

}
