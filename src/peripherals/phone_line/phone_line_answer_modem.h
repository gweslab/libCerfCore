#pragma once

#include "../serial/ppp_link.h"
#include "line_tone.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace cerfcore {

class CerfEmulator;
class PppTerminator;

class PhoneLineAnswerModem : public PppLink {
public:
    enum class Signal : uint8_t { kNone, kAnsam, kAnsamTimedOut, kData };
    enum class Call : uint8_t { kAnswering, kData, kHungUp };

    PhoneLineAnswerModem(CerfEmulator& emu, std::string net_id);
    ~PhoneLineAnswerModem() override;

    void Answer(int64_t at_ns);
    Call CallState() const { return call_; }
    void RestoreCall(Call call, int64_t answer_ns);

    Signal  SignalAt(int64_t at_ns) const;
    void    Tones(int64_t at_ns, std::vector<LineTone>& out) const;
    int64_t NextToneChangeNs(int64_t after_ns) const;

    void SetWake(std::function<void()> wake);
    void StartData();
    void StopData();
    void HangUp();
    void Shutdown();
    void FromSubscriber(const uint8_t* data, size_t n);
    std::vector<uint8_t> TakeForSubscriber();
    void SubscriberDrained();

    void PushRx(const uint8_t* data, size_t n) override;
    bool RxEmpty() const override;
    void SetRxDrainCallback(RxDrainFn cb) override;

private:
    CerfEmulator&                  emu_;
    std::string                    net_id_;
    int64_t                        answer_ns_ = 0;
    Call                           call_      = Call::kAnswering;
    std::unique_ptr<PppTerminator> terminator_;
    std::function<void()>          wake_;
    RxDrainFn                      drain_;
    mutable std::mutex             wire_mu_;
    std::vector<uint8_t>           wire_;
    bool                           in_flight_ = false;
};

}
