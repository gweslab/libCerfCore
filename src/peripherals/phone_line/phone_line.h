#pragma once

#include <cerfcore/host/host_widget.h>
#include "line_tone.h"
#include "phone_line_answer_modem.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

class PhoneLine : public HostWidget {
public:
    static constexpr int64_t kNoChange = INT64_MAX;

    PhoneLine(CerfEmulator& emu, std::string label);

    void RegisterWidget();

    bool Plugged() const { return plugged_.load(std::memory_order_acquire); }

    void Advance(int64_t now_ns);
    void SetOffHook(bool off_hook);
    void DialDigit(int64_t now_ns);

    void SetWake(std::function<void()> wake) {
        wake_ = wake;
        far_end_.SetWake(std::move(wake));
    }
    bool CallUp();
    void FarEndHangUp() { far_end_.HangUp(); }
    void Shutdown() { far_end_.Shutdown(); }
    void StartData() { far_end_.StartData(); }
    void StopData() { far_end_.StopData(); }
    void SendData(const uint8_t* data, size_t n);
    std::vector<uint8_t> TakeIncoming();
    void IncomingDrained() { far_end_.SubscriberDrained(); }

    void    Tones(int64_t at_ns, std::vector<LineTone>& out) const;
    int64_t NextToneChangeNs(int64_t after_ns) const;
    bool    AnswerToneAt(int64_t at_ns) const;

    void SaveLineState(StateWriter& w, int64_t now_ns) const;
    void RestoreLineState(StateReader& r, int64_t now_ns);

    std::string WidgetName() const override { return label_; }
    WidgetGroup Group() const override { return WidgetGroup::Network; }
    std::string Tooltip() const override;
    bool TryPrimaryAction() override;
    bool ControlsAreOneAction() const override { return true; }
    std::vector<WidgetControl> Controls() override;
    WidgetIcon Icon() const override;
    bool PollDirty() override;
    void SaveWidgetState(StateWriter& w) const override;
    void RestoreWidgetState(StateReader& r) override;

private:
    enum class Phase : uint8_t { kSilent, kDialTone, kCollecting, kRingback, kAnswered };

    void    Toggle();
    void    Sync();
    void    StartCall();
    bool    Connected() const { return off_hook_ && line_plugged_; }
    Phase   PhaseAt(int64_t at_ns) const;
    int64_t RouteNs() const { return last_digit_ns_ + kEndOfDialingNs; }
    int64_t AnswerNs() const { return RouteNs() + kRingOnNs; }

    static constexpr int64_t kEndOfDialingNs = 3'000'000'000;
    static constexpr int64_t kRingOnNs       = 2'000'000'000;

    CerfEmulator&        emu_;
    std::string          label_;
    std::atomic<bool>    plugged_{true};
    bool                 drawn_plugged_ = true;
    bool                 line_plugged_  = true;
    bool                 off_hook_      = false;
    uint32_t             digits_        = 0;
    int64_t              last_digit_ns_ = 0;
    PhoneLineAnswerModem far_end_;
    std::function<void()> wake_;
};

}
