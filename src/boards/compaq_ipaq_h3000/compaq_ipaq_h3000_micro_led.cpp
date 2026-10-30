#include "compaq_ipaq_h3000_micro_led.h"

#include "compaq_ipaq_h3000_battery.h"
#include "compaq_ipaq_h3000_family_id.h"
#include "../../core/steady_time.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint8_t   kColorYellow       = 0;
constexpr uint8_t   kColorGreen        = 1;
constexpr uint64_t  kMicrosPerTick     = 100000;
constexpr uint64_t  kTicksForZero      = 256;
constexpr uint64_t  kChargeBlinkTicks  = 20;
constexpr WidgetRgb kClrGreen{120, 255, 140};
constexpr WidgetRgb kClrYellow{255, 200, 40};
constexpr WidgetRgb kClrDark{34, 44, 36};
constexpr WidgetRgb kClrRim{150, 160, 150};
constexpr int       kRadius = 6;

uint64_t Ticks(uint8_t field) { return field == 0u ? kTicksForZero : field; }

}

REGISTER_SERVICE(CompaqIpaqH3000MicroLed);

bool CompaqIpaqH3000MicroLed::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

void CompaqIpaqH3000MicroLed::OnReady() {
    battery_ = &emu_.Get<CompaqIpaqH3000Battery>();
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void CompaqIpaqH3000MicroLed::SetLed(const uint8_t* req, uint8_t len) {
    if (len != 4) {
        emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroLed: LED request of %u bytes", len);
    }
    if (req[0] != kColorYellow && req[0] != kColorGreen) {
        emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroLed: LED request %02X %02X %02X %02X",
                              req[0], req[1], req[2], req[3]);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    led_ = Setting{req[0], req[1], req[2], req[3]};
}

void CompaqIpaqH3000MicroLed::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("micro_led_color", led_.color);
    w.Write("micro_led_flags", led_.flags);
    w.Write("micro_led_on_time", led_.on_time);
    w.Write("micro_led_off_time", led_.off_time);
}

void CompaqIpaqH3000MicroLed::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("micro_led_color", led_.color);
    r.Read("micro_led_flags", led_.flags);
    r.Read("micro_led_on_time", led_.on_time);
    r.Read("micro_led_off_time", led_.off_time);
}

CompaqIpaqH3000MicroLed::Setting CompaqIpaqH3000MicroLed::Current() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return led_;
}

CompaqIpaqH3000MicroLed::Glow CompaqIpaqH3000MicroLed::GlowNow() const {
    const Setting  s    = Current();
    const uint64_t tick = HostSteadyMicros() / kMicrosPerTick;
    if (s.color == kColorGreen) {
        const uint64_t on = Ticks(s.on_time);
        return tick % (on + Ticks(s.off_time)) < on ? Glow::kGreen : Glow::kDark;
    }
    if (!battery_->OnAc()) return Glow::kDark;
    if (!battery_->Charging()) return Glow::kYellow;
    return tick % kChargeBlinkTicks < kChargeBlinkTicks / 2 ? Glow::kYellow : Glow::kDark;
}

std::string CompaqIpaqH3000MicroLed::Tooltip() const {
    if (Current().color == kColorGreen) return "Alarm/charge LED - alarm";
    if (!battery_->OnAc()) return "Alarm/charge LED - off";
    return battery_->Charging() ? "Alarm/charge LED - charging" : "Alarm/charge LED - charged";
}

WidgetIcon CompaqIpaqH3000MicroLed::Icon() const {
    switch (GlowNow()) {
        case Glow::kGreen:  return LedIcon{kClrGreen, kClrRim, kRadius};
        case Glow::kYellow: return LedIcon{kClrYellow, kClrRim, kRadius};
        case Glow::kDark:   break;
    }
    return LedIcon{kClrDark, kClrRim, kRadius};
}

bool CompaqIpaqH3000MicroLed::PollDirty() {
    const Glow glow = GlowNow();
    if (drawn_once_ && glow == last_drawn_) return false;
    drawn_once_ = true;
    last_drawn_ = glow;
    return true;
}

}
