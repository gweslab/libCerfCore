#include "phone_line.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/state/state_stream.h>

#include <cmath>
#include <utility>

namespace cerfcore {

namespace {

constexpr double kToneLevelDbm = -10.0;
constexpr double kDialToneHz[] = {350.0, 440.0};
constexpr double kRingbackHz[] = {440.0, 480.0};

template <size_t N>
void PushPair(const double (&hz)[N], std::vector<LineTone>& out) {
    const double each = kToneLevelDbm - 10.0 * std::log10(static_cast<double>(N));
    for (const double f : hz) out.push_back({f, each});
}

}

PhoneLine::PhoneLine(CerfEmulator& emu, std::string label)
    : emu_(emu), label_(std::move(label)), far_end_(emu, "ppp:phone:" + label_) {}

void PhoneLine::SendData(const uint8_t* data, size_t n) {
    MarkTx();
    far_end_.FromSubscriber(data, n);
}

std::vector<uint8_t> PhoneLine::TakeIncoming() {
    std::vector<uint8_t> bytes = far_end_.TakeForSubscriber();
    if (!bytes.empty()) MarkRx();
    return bytes;
}

void PhoneLine::RegisterWidget() {
    emu_.Get<HostWidgetRegistry>().Register(this);
}

void PhoneLine::Sync() {
    const bool plugged = Plugged();
    if (plugged == line_plugged_) return;
    line_plugged_ = plugged;
    StartCall();
}

bool PhoneLine::CallUp() {
    Sync();
    return Connected();
}

void PhoneLine::Advance(int64_t now_ns) {
    Sync();
    const Phase phase = PhaseAt(now_ns);
    if (phase == Phase::kAnswered &&
        far_end_.SignalAt(now_ns) == PhoneLineAnswerModem::Signal::kAnsamTimedOut)
        far_end_.HangUp();
    const bool signal = phase == Phase::kDialTone || phase == Phase::kRingback ||
                        (phase == Phase::kAnswered && far_end_.SignalAt(now_ns) == PhoneLineAnswerModem::Signal::kAnsam);
    if (signal) MarkRx();
}

void PhoneLine::StartCall() {
    digits_        = 0;
    last_digit_ns_ = 0;
    far_end_.StopData();
}

void PhoneLine::SetOffHook(bool off_hook) {
    Sync();
    if (off_hook == off_hook_) return;
    off_hook_ = off_hook;
    StartCall();
}

void PhoneLine::DialDigit(int64_t now_ns) {
    Sync();
    if (!Connected()) return;
    if (digits_ != 0 && now_ns >= RouteNs()) return;
    MarkTx();
    ++digits_;
    last_digit_ns_ = now_ns;
    far_end_.Answer(AnswerNs());
}

PhoneLine::Phase PhoneLine::PhaseAt(int64_t at_ns) const {
    if (!Connected()) return Phase::kSilent;
    if (digits_ == 0) return Phase::kDialTone;
    if (at_ns < RouteNs()) return Phase::kCollecting;
    if (at_ns < AnswerNs()) return Phase::kRingback;
    return Phase::kAnswered;
}

void PhoneLine::Tones(int64_t at_ns, std::vector<LineTone>& out) const {
    out.clear();
    switch (PhaseAt(at_ns)) {
        case Phase::kSilent:
        case Phase::kCollecting:
            return;
        case Phase::kDialTone:
            PushPair(kDialToneHz, out);
            return;
        case Phase::kRingback:
            PushPair(kRingbackHz, out);
            return;
        case Phase::kAnswered:
            far_end_.Tones(at_ns, out);
            return;
    }
}

bool PhoneLine::AnswerToneAt(int64_t at_ns) const {
    return PhaseAt(at_ns) == Phase::kAnswered && far_end_.SignalAt(at_ns) == PhoneLineAnswerModem::Signal::kAnsam;
}

int64_t PhoneLine::NextToneChangeNs(int64_t after_ns) const {
    switch (PhaseAt(after_ns)) {
        case Phase::kSilent:
        case Phase::kDialTone:
            return kNoChange;
        case Phase::kCollecting:
            return RouteNs();
        case Phase::kRingback:
            return AnswerNs();
        case Phase::kAnswered:
            return far_end_.NextToneChangeNs(after_ns);
    }
    return kNoChange;
}

void PhoneLine::SaveLineState(StateWriter& w, int64_t now_ns) const {
    w.Write<uint8_t>("line_plugged", line_plugged_ ? 1u : 0u);
    w.Write<uint8_t>("line_off_hook", off_hook_ ? 1u : 0u);
    w.Write<uint32_t>("line_digits", digits_);
    w.Write<int64_t>("line_last_digit_rel_ns", last_digit_ns_ - now_ns);
    w.Write<uint8_t>("line_far_end_call", static_cast<uint8_t>(far_end_.CallState()));
}

void PhoneLine::RestoreLineState(StateReader& r, int64_t now_ns) {
    uint8_t plugged  = 1u;
    uint8_t off_hook = 0u;
    int64_t rel      = 0;
    r.Read("line_plugged", plugged);
    r.Read("line_off_hook", off_hook);
    r.Read("line_digits", digits_);
    r.Read("line_last_digit_rel_ns", rel);
    uint8_t call = 0u;
    r.Read("line_far_end_call", call);
    line_plugged_  = plugged != 0u;
    off_hook_      = off_hook != 0u;
    last_digit_ns_ = now_ns + rel;
    far_end_.RestoreCall(static_cast<PhoneLineAnswerModem::Call>(call), AnswerNs());
}

std::string PhoneLine::Tooltip() const {
    return label_ + (Plugged() ? " - phone line plugged in" : " - phone line unplugged");
}

void PhoneLine::Toggle() {
    plugged_.store(!Plugged(), std::memory_order_release);
    wake_();
}

bool PhoneLine::TryPrimaryAction() {
    Toggle();
    return true;
}

std::vector<WidgetControl> PhoneLine::Controls() {
    ActionControl toggle;
    toggle.id    = "line";
    toggle.label = Plugged() ? "Unplug the phone line" : "Plug in the phone line";
    toggle.press = [this] { Toggle(); };
    return {WidgetControl{std::move(toggle)}};
}

WidgetIcon PhoneLine::Icon() const {
    return ResourceIcon{Plugged() ? "serial_modem" : "modem_disabled"};
}

bool PhoneLine::PollDirty() {
    const bool plugged = Plugged();
    if (plugged == drawn_plugged_) return false;
    drawn_plugged_ = plugged;
    return true;
}

void PhoneLine::SaveWidgetState(StateWriter& w) const {
    w.Write<uint8_t>("plugged", Plugged() ? 1u : 0u);
}

void PhoneLine::RestoreWidgetState(StateReader& r) {
    uint8_t plugged = 1u;
    r.Read("plugged", plugged);
    plugged_.store(plugged != 0u, std::memory_order_release);
}

}
