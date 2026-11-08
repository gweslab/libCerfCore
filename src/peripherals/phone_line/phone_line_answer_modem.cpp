#include "phone_line_answer_modem.h"

#include "../serial/ppp_terminator.h"

#include <limits>
#include <utility>

namespace cerfcore {

namespace {

constexpr int64_t kSilenceNs       = 2'000'000'000;
constexpr int64_t kAnsamNs         = 5'000'000'000;
constexpr double  kAnsamHz         = 2100.0;
constexpr double  kAnsamLevelDbm   = -13.0;
constexpr double  kAnsamAmHz       = 15.0;
constexpr double  kAnsamAmDepth    = 0.2;
constexpr int64_t kAnsamReversalNs = 450'000'000;

}

PhoneLineAnswerModem::PhoneLineAnswerModem(CerfEmulator& emu, std::string net_id)
    : emu_(emu), net_id_(std::move(net_id)) {}

PhoneLineAnswerModem::~PhoneLineAnswerModem() {
    terminator_.reset();
}

void PhoneLineAnswerModem::Answer(int64_t at_ns) {
    answer_ns_ = at_ns;
    call_      = Call::kAnswering;
}

void PhoneLineAnswerModem::RestoreCall(Call call, int64_t answer_ns) {
    StopData();
    answer_ns_ = answer_ns;
    call_      = call == Call::kAnswering ? Call::kAnswering : Call::kHungUp;
}

PhoneLineAnswerModem::Signal PhoneLineAnswerModem::SignalAt(int64_t at_ns) const {
    if (call_ == Call::kData) return Signal::kData;
    if (call_ == Call::kHungUp) return Signal::kNone;
    const int64_t rel = at_ns - answer_ns_;
    if (rel < kSilenceNs) return Signal::kNone;
    if (rel < kSilenceNs + kAnsamNs) return Signal::kAnsam;
    return Signal::kAnsamTimedOut;
}

void PhoneLineAnswerModem::Tones(int64_t at_ns, std::vector<LineTone>& out) const {
    out.clear();
    if (SignalAt(at_ns) != Signal::kAnsam) return;
    out.push_back({kAnsamHz, kAnsamLevelDbm, kAnsamAmHz, kAnsamAmDepth, kAnsamReversalNs, answer_ns_ + kSilenceNs});
}

int64_t PhoneLineAnswerModem::NextToneChangeNs(int64_t after_ns) const {
    if (call_ != Call::kAnswering) return std::numeric_limits<int64_t>::max();
    const int64_t rel = after_ns - answer_ns_;
    if (rel < kSilenceNs) return answer_ns_ + kSilenceNs;
    if (rel < kSilenceNs + kAnsamNs) return answer_ns_ + kSilenceNs + kAnsamNs;
    return std::numeric_limits<int64_t>::max();
}

void PhoneLineAnswerModem::SetWake(std::function<void()> wake) {
    wake_ = std::move(wake);
}

void PhoneLineAnswerModem::StartData() {
    if (call_ == Call::kData) return;
    if (!terminator_) terminator_ = std::make_unique<PppTerminator>(emu_, *this, net_id_);
    {
        std::lock_guard<std::mutex> lk(wire_mu_);
        wire_.clear();
        in_flight_ = false;
    }
    call_ = Call::kData;
    terminator_->Start();
}

void PhoneLineAnswerModem::StopData() {
    if (call_ != Call::kData) return;
    call_ = Call::kHungUp;
    terminator_->Stop();
    std::lock_guard<std::mutex> lk(wire_mu_);
    wire_.clear();
    in_flight_ = false;
}

void PhoneLineAnswerModem::HangUp() {
    StopData();
    call_ = Call::kHungUp;
}

void PhoneLineAnswerModem::Shutdown() {
    StopData();
    terminator_.reset();
}

void PhoneLineAnswerModem::FromSubscriber(const uint8_t* data, size_t n) {
    if (call_ == Call::kData) terminator_->OnGuestData(data, n);
}

std::vector<uint8_t> PhoneLineAnswerModem::TakeForSubscriber() {
    std::lock_guard<std::mutex> lk(wire_mu_);
    std::vector<uint8_t> out;
    out.swap(wire_);
    if (!out.empty()) in_flight_ = true;
    return out;
}

void PhoneLineAnswerModem::SubscriberDrained() {
    {
        std::lock_guard<std::mutex> lk(wire_mu_);
        if (!in_flight_ || !wire_.empty()) return;
        in_flight_ = false;
    }
    drain_();
}

void PhoneLineAnswerModem::PushRx(const uint8_t* data, size_t n) {
    bool was_idle = false;
    {
        std::lock_guard<std::mutex> lk(wire_mu_);
        was_idle = wire_.empty();
        wire_.insert(wire_.end(), data, data + n);
    }
    if (was_idle) wake_();
}

bool PhoneLineAnswerModem::RxEmpty() const {
    std::lock_guard<std::mutex> lk(wire_mu_);
    return wire_.empty() && !in_flight_;
}

void PhoneLineAnswerModem::SetRxDrainCallback(RxDrainFn cb) {
    drain_ = std::move(cb);
}

}
