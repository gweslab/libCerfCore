#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/host/host_widget.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/state/hibernation.h>
#include <cerfengine/host_request_channel.h>
#include "../../core/steady_time.h"
#include "../../execution/engine_bridge.h"
#include "../../socs/guest_sample_clock.h"
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_pfc.h"
#include "../../socs/sh77xx/sh77xx_port_wiring.h"
#include "hp_600_family_id.h"
#include "hp_600_sh7709_battery.h"
#include "hp_600_sh7709_key_matrix.h"
#include "hp_600_sh7709_notification_led_net.h"

#include <atomic>
#include <string>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kLedButtonCell = 7u * Hp600Sh7709KeyMatrix::kColumns + 15u;
constexpr GuestCycleClock::Rate kPressHold{10u, 1u};
constexpr uint64_t  kFlashHalfPeriodMs = 500u;
constexpr WidgetRgb kClrOrange{255, 150, 40};
constexpr WidgetRgb kClrGreen{120, 255, 140};
constexpr WidgetRgb kClrDark{40, 40, 36};
constexpr WidgetRgb kClrRim{150, 160, 150};
constexpr WidgetRgb kClrRimBlink{120, 230, 130};

enum class Charge { kDark, kOrange, kGreen };

class Hp600Sh7709LedButton : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override {
        wiring_   = &emu_.Get<Sh77xxPortWiring>();
        battery_  = &emu_.Get<Hp600Sh7709Battery>();
        matrix_   = &emu_.Get<Hp600Sh7709KeyMatrix>();
        requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
        requests_->RegisterListener([this] { ApplyPress(); });
        hold_.Attach([this] { Release(); });
        emu_.Get<GuestDeepSleep>().RegisterParkClock([this] { ApplyPress(); });
        emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
            press_pending_.store(false, std::memory_order_release);
            hold_.Stop();
        });
        emu_.Get<HostWidgetRegistry>().Register(this);
    }

    std::string WidgetName() const override { return "LED button"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }

    std::string Tooltip() const override {
        std::string text = "LED button - ";
        switch (CurrentCharge()) {
            case Charge::kOrange: text += "orange"; break;
            case Charge::kGreen:  text += "green"; break;
            default:              text += "dark"; break;
        }
        if (Flashing()) text += " and flashing";
        return text + " (click = press the LED button)";
    }

    bool TryPrimaryAction() override {
        Press();
        return true;
    }

    std::vector<WidgetControl> Controls() override {
        ActionControl press;
        press.id    = "press_button";
        press.label = "Press the LED button";
        press.press = [this] { Press(); };
        return {WidgetControl{std::move(press)}};
    }

    WidgetIcon Icon() const override {
        constexpr int kR = 6;
        const WidgetRgb fill = drawn_flash_lit_ ? kClrOrange : ChargeColor(drawn_charge_);
        return LedIcon{fill, drawn_flashing_ ? kClrRimBlink : kClrRim, kR};
    }

    bool PollDirty() override {
        const Charge charge    = CurrentCharge();
        const bool   flashing  = Flashing();
        const bool   flash_lit = flashing && HostSteadySquareWaveHigh(kFlashHalfPeriodMs);
        if (charge == drawn_charge_ && flashing == drawn_flashing_ && flash_lit == drawn_flash_lit_) return false;
        drawn_charge_    = charge;
        drawn_flashing_  = flashing;
        drawn_flash_lit_ = flash_lit;
        return true;
    }

private:
    static WidgetRgb ChargeColor(Charge charge) {
        switch (charge) {
            case Charge::kOrange: return kClrOrange;
            case Charge::kGreen:  return kClrGreen;
            default:              return kClrDark;
        }
    }

    Charge CurrentCharge() const {
        if (battery_->Charging()) return Charge::kOrange;
        return battery_->OnExternalPower() ? Charge::kGreen : Charge::kDark;
    }

    bool Flashing() const { return (wiring_->NetLevels(kSh77xxPortJ) & kHp600Sh7709PortJNotificationLed) == 0u; }

    void Press() {
        press_pending_.store(true, std::memory_order_release);
        requests_->Request();
    }

    void ApplyPress() {
        if (!press_pending_.exchange(false, std::memory_order_acq_rel)) return;
        matrix_->SetKey(kLedButtonCell, true);
        hold_.Start(kPressHold);
    }

    void Release() {
        hold_.Stop();
        matrix_->SetKey(kLedButtonCell, false);
    }

    Sh77xxPortWiring*                wiring_   = nullptr;
    Hp600Sh7709Battery*              battery_  = nullptr;
    Hp600Sh7709KeyMatrix*            matrix_   = nullptr;
    cerfengine::HostRequestChannel*  requests_ = nullptr;
    GuestSampleClock                 hold_{emu_};
    std::atomic<bool>                press_pending_{false};
    Charge                           drawn_charge_    = Charge::kDark;
    bool                             drawn_flashing_  = false;
    bool                             drawn_flash_lit_ = false;
};

}

REGISTER_SERVICE(Hp600Sh7709LedButton);

}
