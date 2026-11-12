#include "ps2_mouse.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <limits>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint8_t kAck       = 0xFAu;
constexpr uint8_t kResend    = 0xFEu;
constexpr uint8_t kError     = 0xFCu;
constexpr uint8_t kBatPassed = 0xAAu;
constexpr uint8_t kDeviceId  = 0x00u;

constexpr uint8_t kCmdReset       = 0xFFu;
constexpr uint8_t kCmdResend      = 0xFEu;
constexpr uint8_t kCmdSetDefaults = 0xF6u;
constexpr uint8_t kCmdDisable     = 0xF5u;
constexpr uint8_t kCmdEnable      = 0xF4u;
constexpr uint8_t kCmdSampleRate  = 0xF3u;
constexpr uint8_t kCmdDeviceId    = 0xF2u;
constexpr uint8_t kCmdRemote      = 0xF0u;
constexpr uint8_t kCmdWrap        = 0xEEu;
constexpr uint8_t kCmdResetWrap   = 0xECu;
constexpr uint8_t kCmdReadData    = 0xEBu;
constexpr uint8_t kCmdStream      = 0xEAu;
constexpr uint8_t kCmdStatus      = 0xE9u;
constexpr uint8_t kCmdResolution  = 0xE8u;
constexpr uint8_t kCmdScaling21   = 0xE7u;
constexpr uint8_t kCmdScaling11   = 0xE6u;

constexpr uint8_t                kDefaultSampleRate = 100u;
constexpr uint8_t                kDefaultResolution = 2u;
constexpr uint8_t                kMaxResolution     = 3u;
constexpr std::array<uint8_t, 7> kSampleRates       = {10u, 20u, 40u, 60u, 80u, 100u, 200u};

constexpr uint8_t kStatusRemote  = 0x40u;
constexpr uint8_t kStatusEnable  = 0x20u;
constexpr uint8_t kStatusScaling = 0x10u;
constexpr uint8_t kStatusLeft    = 0x04u;
constexpr uint8_t kStatusRight   = 0x01u;

constexpr uint8_t kPacketYSign   = 0x20u;
constexpr uint8_t kPacketXSign   = 0x10u;
constexpr uint8_t kPacketAlways1 = 0x08u;
constexpr uint8_t kPacketRight   = 0x02u;
constexpr uint8_t kPacketLeft    = 0x01u;

constexpr int32_t kPacketLimit       = 127;
constexpr int32_t kScaledPacketLimit = 63;

int32_t Scale2To1(int32_t v) {
    constexpr std::array<int32_t, 6> kSmall = {0, 1, 1, 3, 6, 9};
    const int32_t magnitude = std::abs(v);
    const int32_t reported  = magnitude < 6 ? kSmall[magnitude] : 2 * magnitude;
    return v < 0 ? -reported : reported;
}

std::vector<uint8_t> MotionPacket(uint8_t buttons, int32_t x, int32_t y) {
    uint8_t b0 = static_cast<uint8_t>(kPacketAlways1 | buttons);
    if (x < 0) b0 |= kPacketXSign;
    if (y < 0) b0 |= kPacketYSign;
    return {b0, static_cast<uint8_t>(x), static_cast<uint8_t>(y)};
}

uint8_t ButtonBits(uint32_t mask) {
    uint8_t bits = 0;
    if (mask & Ps2Mouse::kButtonLeft) bits |= kPacketLeft;
    if (mask & Ps2Mouse::kButtonRight) bits |= kPacketRight;
    return bits;
}

void AddMotion(int32_t& count, int64_t delta) {
    count = static_cast<int32_t>(std::clamp<int64_t>(int64_t{count} + delta, std::numeric_limits<int32_t>::min(),
                                                     std::numeric_limits<int32_t>::max()));
}

Ps2MouseSample CutPart(int32_t& x, int32_t& y, uint32_t buttons, int32_t limit) {
    const Ps2MouseSample part{std::clamp(x, -limit, limit), std::clamp(y, -limit, limit), buttons};
    x -= part.x;
    y -= part.y;
    return part;
}

std::vector<Ps2MouseSample> SplitReport(Ps2MouseSample report, int32_t limit) {
    std::vector<Ps2MouseSample> parts;
    do {
        parts.push_back(CutPart(report.x, report.y, report.buttons, limit));
    } while (report.x != 0 || report.y != 0);
    return parts;
}

}

Ps2Mouse::Ps2Mouse(std::function<void()> on_data, HaltFn halt, Ps2MouseTraits traits)
    : on_data_(std::move(on_data)), halt_(std::move(halt)), traits_(traits) {}

void Ps2Mouse::WriteCommand(uint8_t byte) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (wrap_ && byte != kCmdResetWrap && byte != kCmdReset) {
        out_.push_back(byte);
        last_packet_ = {byte};
        return;
    }
    if (pending_command_ != 0u) {
        ParameterLocked(byte);
        return;
    }
    CommandLocked(byte);
}

void Ps2Mouse::CommandLocked(uint8_t b) {
    switch (b) {
        case kCmdReset:
            out_.clear();
            SetDefaultsLocked();
            remote_        = false;
            wrap_          = false;
            invalid_count_ = 0;
            ResetCountersLocked();
            out_.push_back(kAck);
            out_.push_back(kBatPassed);
            out_.push_back(kDeviceId);
            last_packet_ = {kBatPassed, kDeviceId};
            return;
        case kCmdResend:
            if (last_packet_.empty()) halt_("PS/2 resend before any packet", b);
            invalid_count_ = 0;
            out_.insert(out_.end(), last_packet_.begin(), last_packet_.end());
            return;
        case kCmdSetDefaults:
            SetDefaultsLocked();
            remote_ = false;
            AcknowledgeLocked({});
            break;
        case kCmdDisable:  reporting_ = false; AcknowledgeLocked({}); break;
        case kCmdEnable:   reporting_ = true;  AcknowledgeLocked({}); break;
        case kCmdSampleRate:
        case kCmdResolution:
            pending_command_ = b;
            AcknowledgeLocked({});
            break;
        case kCmdDeviceId: AcknowledgeLocked({kDeviceId}); break;
        case kCmdRemote:   remote_ = true; AcknowledgeLocked({}); break;
        case kCmdWrap:     wrap_ = true;   AcknowledgeLocked({}); break;
        case kCmdResetWrap:
            if (wrap_) {
                wrap_ = false;
                if (traits_.reset_wrap_disables_stream && !remote_) reporting_ = false;
            }
            AcknowledgeLocked({});
            break;
        case kCmdReadData:  AcknowledgeLocked(ReadDataPacketLocked()); break;
        case kCmdStream:    remote_ = false; AcknowledgeLocked({}); break;
        case kCmdStatus:    AcknowledgeLocked({StatusLocked(), resolution_, sample_rate_}); break;
        case kCmdScaling21: scaling_2to1_ = true;  AcknowledgeLocked({}); break;
        case kCmdScaling11: scaling_2to1_ = false; AcknowledgeLocked({}); break;
        default:
            RejectLocked();
            return;
    }
    ResetCountersLocked();
}

void Ps2Mouse::ParameterLocked(uint8_t b) {
    const bool valid = pending_command_ == kCmdSampleRate
                           ? std::find(kSampleRates.begin(), kSampleRates.end(), b) != kSampleRates.end()
                           : b <= kMaxResolution;
    if (!valid) {
        RejectLocked();
        return;
    }
    if (pending_command_ == kCmdSampleRate) {
        sample_rate_ = b;
    } else {
        resolution_ = b;
    }
    pending_command_ = 0;
    AcknowledgeLocked({});
    ResetCountersLocked();
}

void Ps2Mouse::AcknowledgeLocked(std::vector<uint8_t> data) {
    invalid_count_ = 0;
    out_.push_back(kAck);
    if (data.empty()) {
        last_packet_ = {kAck};
        return;
    }
    out_.insert(out_.end(), data.begin(), data.end());
    last_packet_ = std::move(data);
}

bool Ps2Mouse::RejectLocked() {
    if (invalid_count_ < 2u) ++invalid_count_;
    const bool error = invalid_count_ >= 2u;
    out_.push_back(error ? kError : kResend);
    if (error) pending_command_ = 0;
    return error;
}

void Ps2Mouse::SetDefaultsLocked() {
    sample_rate_  = kDefaultSampleRate;
    resolution_   = kDefaultResolution;
    scaling_2to1_ = false;
    reporting_    = false;
}

void Ps2Mouse::ZeroCountsLocked() {
    count_x_ = 0;
    count_y_ = 0;
}

void Ps2Mouse::ResetCountersLocked() {
    ZeroCountsLocked();
    queued_.Clear();
}

bool Ps2Mouse::StreamingLocked() const { return reporting_ && !remote_ && !wrap_; }

bool Ps2Mouse::MovedLocked() const { return count_x_ != 0 || count_y_ != 0; }

uint8_t Ps2Mouse::StatusLocked() const {
    uint8_t s = 0;
    if (remote_) s |= kStatusRemote;
    if (reporting_) s |= kStatusEnable;
    if (scaling_2to1_) s |= kStatusScaling;
    if (buttons_ & kButtonLeft) s |= kStatusLeft;
    if (buttons_ & kButtonRight) s |= kStatusRight;
    return s;
}

std::vector<uint8_t> Ps2Mouse::ReadDataPacketLocked() const {
    int32_t              x    = count_x_;
    int32_t              y    = count_y_;
    const Ps2MouseSample part = CutPart(x, y, buttons_, kPacketLimit);
    return MotionPacket(ButtonBits(part.buttons), part.x, part.y);
}

bool Ps2Mouse::HasData() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return !out_.empty();
}

uint8_t Ps2Mouse::ReadData() {
    bool    raise = false;
    uint8_t b     = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (out_.empty()) return 0;
        b = out_.front();
        out_.pop_front();
        raise = StreamingLocked() && !out_.empty();
    }
    if (raise) RaiseData();
    return b;
}

void Ps2Mouse::QueueMotion(int dx, int dy, uint32_t button_mask) {
    std::lock_guard<std::mutex> lk(mtx_);
    if (button_mask != buttons_) QueueReportLocked();
    buttons_ = button_mask;
    AddMotion(count_x_, dx);
    AddMotion(count_y_, -int64_t{dy});
}

void Ps2Mouse::QueueReportLocked() {
    if (!StreamingLocked()) return;
    const uint32_t last = queued_.Empty() ? reported_buttons_ : queued_.Back().buttons;
    if (buttons_ == last && !MovedLocked()) return;
    queued_.Push({count_x_, count_y_, buttons_});
    ZeroCountsLocked();
}

bool Ps2Mouse::TakeSampleLocked(Ps2MouseSample& sample) {
    if (!StreamingLocked()) return false;
    if (!queued_.Empty()) {
        sample = queued_.Pop();
    } else {
        if (!MovedLocked() && buttons_ == reported_buttons_) return false;
        sample = {count_x_, count_y_, buttons_};
        ZeroCountsLocked();
    }
    reported_buttons_ = sample.buttons;
    return true;
}

std::vector<Ps2MouseSample> Ps2Mouse::TakeSample() {
    std::lock_guard<std::mutex> lk(mtx_);
    Ps2MouseSample report;
    if (!TakeSampleLocked(report)) return {};
    return SplitReport(report, kPacketLimit);
}

void Ps2Mouse::Sample() {
    bool raise = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        Ps2MouseSample report;
        if (!TakeSampleLocked(report)) return;
        const bool scaled    = scaling_2to1_ && traits_.applies_scaling;
        const bool was_empty = out_.empty();
        for (Ps2MouseSample part : SplitReport(report, scaled ? kScaledPacketLimit : kPacketLimit)) {
            if (scaled) {
                part.x = Scale2To1(part.x);
                part.y = Scale2To1(part.y);
            }
            last_packet_ = MotionPacket(ButtonBits(part.buttons), part.x, part.y);
            out_.insert(out_.end(), last_packet_.begin(), last_packet_.end());
        }
        raise = was_empty;
    }
    if (raise) RaiseData();
}

uint8_t Ps2Mouse::SampleRate() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return sample_rate_;
}

bool Ps2Mouse::AwaitsCommand() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return pending_command_ == 0u && !wrap_;
}

void Ps2Mouse::Acknowledge(std::vector<uint8_t> data) {
    std::lock_guard<std::mutex> lk(mtx_);
    AcknowledgeLocked(std::move(data));
}

bool Ps2Mouse::Reject() {
    std::lock_guard<std::mutex> lk(mtx_);
    return RejectLocked();
}

void Ps2Mouse::AbortCommand() {
    std::lock_guard<std::mutex> lk(mtx_);
    pending_command_ = 0;
    invalid_count_   = 0;
}

void Ps2Mouse::StartStreamReporting() {
    std::lock_guard<std::mutex> lk(mtx_);
    remote_    = false;
    wrap_      = false;
    reporting_ = true;
    ResetCountersLocked();
}

void Ps2Mouse::Reset() {
    std::lock_guard<std::mutex> lk(mtx_);
    out_.clear();
    last_packet_.clear();
    SetDefaultsLocked();
    remote_          = false;
    wrap_            = false;
    pending_command_  = 0;
    invalid_count_    = 0;
    buttons_          = 0;
    reported_buttons_ = 0;
    ResetCountersLocked();
}

void Ps2Mouse::SaveState(StateWriter& w) const {
    std::lock_guard<std::mutex> lk(mtx_);
    const std::vector<uint8_t> out(out_.begin(), out_.end());
    w.Write<uint32_t>("ps2_out_size", static_cast<uint32_t>(out.size()));
    w.WriteBytes("ps2_out", out.data(), out.size());
    w.Write<uint32_t>("ps2_last_packet_size", static_cast<uint32_t>(last_packet_.size()));
    w.WriteBytes("ps2_last_packet", last_packet_.data(), last_packet_.size());
    w.Write("ps2_pending_command", pending_command_);
    w.Write("ps2_invalid_count", invalid_count_);
    w.Write<uint8_t>("ps2_reporting", reporting_ ? 1u : 0u);
    w.Write<uint8_t>("ps2_remote", remote_ ? 1u : 0u);
    w.Write<uint8_t>("ps2_wrap", wrap_ ? 1u : 0u);
    w.Write<uint8_t>("ps2_scaling_2to1", scaling_2to1_ ? 1u : 0u);
    w.Write("ps2_sample_rate", sample_rate_);
    w.Write("ps2_resolution", resolution_);
    w.Write("ps2_motion_x", count_x_);
    w.Write("ps2_motion_y", count_y_);
}

void Ps2Mouse::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    uint32_t out_size         = 0;
    uint32_t last_packet_size = 0;
    uint8_t  reporting        = 0;
    uint8_t  remote           = 0;
    uint8_t  wrap             = 0;
    uint8_t  scaling          = 0;
    r.Read("ps2_out_size", out_size);
    std::vector<uint8_t> out(out_size);
    r.ReadBytes("ps2_out", out.data(), out.size());
    out_.assign(out.begin(), out.end());
    r.Read("ps2_last_packet_size", last_packet_size);
    last_packet_.resize(last_packet_size);
    r.ReadBytes("ps2_last_packet", last_packet_.data(), last_packet_.size());
    r.Read("ps2_pending_command", pending_command_);
    r.Read("ps2_invalid_count", invalid_count_);
    r.Read("ps2_reporting", reporting);
    r.Read("ps2_remote", remote);
    r.Read("ps2_wrap", wrap);
    r.Read("ps2_scaling_2to1", scaling);
    r.Read("ps2_sample_rate", sample_rate_);
    r.Read("ps2_resolution", resolution_);
    r.Read("ps2_motion_x", count_x_);
    r.Read("ps2_motion_y", count_y_);
    buttons_          = 0;
    reported_buttons_ = 0;
    queued_.Clear();
    reporting_    = reporting != 0u;
    remote_       = remote != 0u;
    wrap_         = wrap != 0u;
    scaling_2to1_ = scaling != 0u;
}

void Ps2Mouse::RaiseData() {
    if (on_data_) on_data_();
}

}
