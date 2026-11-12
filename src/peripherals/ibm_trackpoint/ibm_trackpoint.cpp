#include "ibm_trackpoint.h"

#include "../serial/uart_frame.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <vector>

namespace cerfcore {

namespace {

using Parity = SerialLine::LineConfig::Parity;

constexpr uint8_t kController      = 0xE2u;
constexpr uint8_t kReadExtendedId  = 0xD0u;
constexpr uint8_t kReadSecondaryId = 0xE1u;
constexpr uint8_t kSerialBit7      = 0x80u;

constexpr uint8_t kE2LastRamRead = 0x3Fu;
constexpr uint8_t kE2BitToggle   = 0x47u;
constexpr uint8_t kE2Ps2Mode     = 0x53u;
constexpr uint8_t kE2RamRead     = 0x80u;
constexpr uint8_t kE2RamWrite    = 0x81u;

constexpr uint32_t kSerialBaud = 1200u;

constexpr uint32_t kSerialReportHz = 40u;
constexpr uint32_t kXmitGapWrap    = 256u;
constexpr uint8_t  kSerialSync     = 0x40u;
constexpr uint8_t  kSerialLeft     = 0x20u;
constexpr uint8_t  kSerialRight    = 0x10u;
constexpr uint8_t  kSerialLowBits  = 0x3Fu;
constexpr unsigned kSerialHighShift = 6u;
constexpr unsigned kSerialYHighPos  = 2u;

constexpr uint8_t kRamConfig      = 0x2Cu;
constexpr uint8_t kRamXmitGap     = 0x62u;
constexpr uint8_t kXmitGapDefault = 0x09u;
constexpr uint8_t kPressToSelect  = 0x01u;

constexpr Ps2MouseTraits kTrackpointTraits = {true, false};

constexpr std::array<uint8_t, 76> kSerialMouseId = {
    0x4D, 0x33, 0x00, 0x11, 0x19, 0x19, 0x18, 0x10, 0x15, 0x10, 0x11, 0x00, 0x32, 0x33, 0x2F,
    0x08, 0x01, 0x24, 0x29, 0x22, 0x2D, 0x13, 0x17, 0x18, 0x13, 0x3C, 0x3C, 0x2D, 0x2F, 0x35,
    0x33, 0x25, 0x3C, 0x30, 0x2E, 0x30, 0x10, 0x26, 0x11, 0x17, 0x3C, 0x29, 0x22, 0x2D, 0x00,
    0x34, 0x32, 0x21, 0x23, 0x2B, 0x30, 0x2F, 0x29, 0x2E, 0x34, 0x00, 0x36, 0x25, 0x32, 0x33,
    0x29, 0x2F, 0x2E, 0x00, 0x14, 0x0E, 0x10, 0x00, 0x39, 0x2B, 0x34, 0x13, 0x23, 0x14, 0x15,
    0x09};

constexpr std::array<uint8_t, 13> kUnmodeledE2 = {0x40, 0x41, 0x44, 0x45, 0x46, 0x4B, 0x4C,
                                                  0x4E, 0x4F, 0x51, 0x52, 0x7F, 0xB9};
constexpr std::array<uint8_t, 11> kWritableRam = {0x41, 0x42, 0x4A, 0x58, 0x59, 0x5A,
                                                  0x5C, 0x5D, 0x5E, 0x60, 0x62};

static_assert(IbmTrackpoint::kLineTickHz % kSerialBaud == 0u);

template <size_t N>
bool Contains(const std::array<uint8_t, N>& set, uint8_t b) {
    return std::find(set.begin(), set.end(), b) != set.end();
}

}

IbmTrackpoint::IbmTrackpoint(IbmTrackpointHost& host, GuestSampleClock& samples)
    : host_(host),
      samples_(samples),
      ps2_(nullptr, [this](const char* what, unsigned value) { host_.Halt(what, value); },
           kTrackpointTraits) {}

void IbmTrackpoint::PowerOn() {
    mode_       = Mode::kSerial;
    baud_param_ = 0;
    ram_.fill(0);
    ram_known_.fill(0);
    ram_[kRamConfig]        = 0;
    ram_known_[kRamConfig]  = 1;
    ram_[kRamXmitGap]       = kXmitGapDefault;
    ram_known_[kRamXmitGap] = 1;
    ps2_.Reset();
    ps2_.StartStreamReporting();
    parse_          = Parse::kCommand;
    e2_command_     = 0;
    e2_arg1_        = 0;
    id_in_progress_ = false;
    sample_due_     = false;
    pending_ps2_    = false;
    pending_param_  = 0;
    tx_queue_.clear();
    has_last_land_ = false;
    last_land_     = 0;
    post_end_      = Now() + static_cast<int64_t>(kLineTickHz * kPostMs / kMsPerSecond);
    Disarm();
}

void IbmTrackpoint::FinishSelfTest() { post_end_ = Now(); }

bool IbmTrackpoint::InPost() const { return Now() < post_end_; }

void IbmTrackpoint::SetLines(bool dtr_high, bool rts_high) {
    const bool rts_fell = rts_high_ && !rts_high;
    dtr_high_ = dtr_high;
    rts_high_ = rts_high;
    if (InPost()) return;
    if (rts_fell) {
        SerialMouseReset();
    } else if (dtr_high_ && armed_ && start_ > Now()) {
        Disarm();
    }
    StartTransmit();
}

void IbmTrackpoint::HostMotion(int dx, int dy, uint32_t buttons) {
    ps2_.QueueMotion(dx, dy, buttons);
}

void IbmTrackpoint::OnLineTick() {
    while (armed_ && land_ <= Now()) LandHead();
}

void IbmTrackpoint::OnSample() {
    if (!InPost()) {
        if (LineBusy()) sample_due_ = true;
        else            TakeReport();
    }
    samples_.SetRate({SampleRateHz(), 1u});
}

uint32_t IbmTrackpoint::SampleRateHz() const {
    return mode_ == Mode::kSerial ? kSerialReportHz : ps2_.SampleRate();
}

void IbmTrackpoint::RearmTransmit() {
    host_.ArmLineTick(armed_ ? static_cast<uint64_t>(std::max<int64_t>(land_, 0))
                             : IbmTrackpointHost::kNoEvent);
}

void IbmTrackpoint::OnGuestTx(uint8_t byte) {
    const SerialLine::LineConfig host = host_.HostFormat();
    const SerialLine::LineConfig own  = Format();
    if (InPost()) return;
    if (host.baud != own.baud) host_.Halt("host baud rate", host.baud);
    if (id_in_progress_) return;
    if (pending_ps2_) host_.Halt("byte before the PS/2 mode ACK was sent", byte);
    DiscardUntransmitted();
    const UartDecodedFrame frame = DecodeUartFrame(host, byte, own);
    if (frame.parity_error || frame.framing_error) {
        Reject();
    } else {
        OnByte(frame.data);
    }
    QueueResponse(true);
    StartTransmit();
}

SerialLine::LineConfig IbmTrackpoint::Format() const {
    SerialLine::LineConfig f;
    f.data_bits = 8;
    f.stop      = SerialLine::LineConfig::Stop::One;
    if (mode_ == Mode::kSerial) {
        f.baud   = kSerialBaud;
        f.parity = Parity::None;
    } else {
        f.baud   = kPs2BaudClock / (kPs2BaudDivisor * (256u - baud_param_));
        f.parity = Parity::Odd;
    }
    return f;
}

int64_t IbmTrackpoint::Now() const {
    return static_cast<int64_t>(host_.LineTickNow());
}

int64_t IbmTrackpoint::FrameTicks() const {
    const uint64_t bit = mode_ == Mode::kSerial
                             ? kLineTickHz / kSerialBaud
                             : kLineTickHz * kPs2BaudDivisor * (256u - baud_param_) / kPs2BaudClock;
    return static_cast<int64_t>(bit * UartFrameBits(Format()));
}

int64_t IbmTrackpoint::GapTicks() const {
    const uint32_t units = ram_[kRamXmitGap] != 0u ? ram_[kRamXmitGap] : kXmitGapWrap;
    return static_cast<int64_t>(units * kLineTickHz * kXmitGapUnitUs / kUsPerSecond);
}

void IbmTrackpoint::SerialMouseReset() {
    mode_           = Mode::kSerial;
    pending_ps2_    = false;
    parse_          = Parse::kCommand;
    id_in_progress_ = true;
    ps2_.AbortCommand();
    while (ps2_.HasData()) ps2_.ReadData();
    ps2_.StartStreamReporting();
    tx_queue_.clear();
    Disarm();
    for (const uint8_t b : kSerialMouseId) tx_queue_.push_back({b, false});
}

void IbmTrackpoint::OnByte(uint8_t b) {
    switch (parse_) {
        case Parse::kCommand: OnCommand(b); break;
        case Parse::kE2Sub:   OnE2Sub(b);   break;
        case Parse::kE2Arg1:  OnE2Arg1(b);  break;
        case Parse::kE2Arg2:  OnE2Arg2(b);  break;
    }
}

void IbmTrackpoint::OnCommand(uint8_t b) {
    if (ps2_.AwaitsCommand()) {
        if (b == kController) {
            ps2_.Acknowledge();
            parse_ = Parse::kE2Sub;
            return;
        }
        if (b == kReadExtendedId || b == kReadSecondaryId) host_.Halt("command", b);
    }
    ps2_.WriteCommand(b);
}

void IbmTrackpoint::OnE2Sub(uint8_t b) {
    if (b <= kE2LastRamRead) {
        ps2_.Acknowledge({ReadRam(b)});
        parse_ = Parse::kCommand;
        return;
    }
    if (b == kE2BitToggle || b == kE2Ps2Mode || b == kE2RamRead || b == kE2RamWrite) {
        e2_command_ = b;
        ps2_.Acknowledge();
        parse_ = Parse::kE2Arg1;
        return;
    }
    if (Contains(kUnmodeledE2, b)) host_.Halt("E2 command", b);
    Reject();
}

void IbmTrackpoint::OnE2Arg1(uint8_t b) {
    if (e2_command_ == kE2Ps2Mode) {
        ps2_.Acknowledge();
        pending_ps2_   = true;
        pending_param_ = b;
        parse_         = Parse::kCommand;
        return;
    }
    if (e2_command_ == kE2RamRead) {
        ps2_.Acknowledge({ReadRam(b)});
        parse_ = Parse::kCommand;
        return;
    }
    e2_arg1_ = b;
    ps2_.Acknowledge();
    parse_ = Parse::kE2Arg2;
}

void IbmTrackpoint::OnE2Arg2(uint8_t b) {
    const unsigned operands = (static_cast<unsigned>(e2_arg1_) << 8) | b;
    if (e2_command_ == kE2BitToggle) {
        if (e2_arg1_ != kRamConfig || b != kPressToSelect) host_.Halt("E2 47 bit toggle", operands);
        ram_[kRamConfig] = static_cast<uint8_t>(ram_[kRamConfig] ^ b);
    } else {
        if (!Contains(kWritableRam, e2_arg1_)) host_.Halt("E2 81 RAM write", operands);
        ram_[e2_arg1_]       = b;
        ram_known_[e2_arg1_] = 1;
    }
    ps2_.Acknowledge();
    parse_ = Parse::kCommand;
}

void IbmTrackpoint::Reject() {
    if (ps2_.Reject()) parse_ = Parse::kCommand;
}

void IbmTrackpoint::QueueResponse(bool reply) {
    for (bool first = reply; ps2_.HasData(); first = false) tx_queue_.push_back({ps2_.ReadData(), first});
}

bool IbmTrackpoint::LineBusy() const {
    return !tx_queue_.empty() || id_in_progress_ || pending_ps2_;
}

void IbmTrackpoint::TakeReport() {
    if (mode_ == Mode::kSerial) {
        for (const Ps2MouseSample& s : ps2_.TakeSample()) QueueSerialReport(s);
    } else {
        ps2_.Sample();
        QueueResponse(false);
    }
    StartTransmit();
}

void IbmTrackpoint::QueueSerialReport(const Ps2MouseSample& s) {
    const uint8_t x = static_cast<uint8_t>(s.x);
    const uint8_t y = static_cast<uint8_t>(-s.y);
    uint8_t       head = kSerialSync;
    if (s.buttons & Ps2Mouse::kButtonLeft) head = static_cast<uint8_t>(head | kSerialLeft);
    if (s.buttons & Ps2Mouse::kButtonRight) head = static_cast<uint8_t>(head | kSerialRight);
    head = static_cast<uint8_t>(head | ((y >> kSerialHighShift) << kSerialYHighPos) |
                                (x >> kSerialHighShift));
    tx_queue_.push_back({head, false});
    tx_queue_.push_back({static_cast<uint8_t>(x & kSerialLowBits), false});
    tx_queue_.push_back({static_cast<uint8_t>(y & kSerialLowBits), false});
}

void IbmTrackpoint::DiscardUntransmitted() {
    if (armed_ && start_ <= Now()) {
        tx_queue_.erase(tx_queue_.begin() + 1, tx_queue_.end());
        return;
    }
    tx_queue_.clear();
    Disarm();
}

void IbmTrackpoint::StartTransmit() {
    if (armed_ || dtr_high_ || tx_queue_.empty()) return;
    ArmHead(Now());
}

void IbmTrackpoint::ArmHead(int64_t ready) {
    start_ = has_last_land_ ? std::max(ready, last_land_ + GapTicks()) : ready;
    land_  = tx_queue_.front().first ? start_ : start_ + FrameTicks();
    armed_ = true;
    RearmTransmit();
}

void IbmTrackpoint::Disarm() {
    armed_ = false;
    RearmTransmit();
}

void IbmTrackpoint::LandHead() {
    armed_ = false;
    const SerialLine::LineConfig format = Format();
    uint8_t                      b      = tx_queue_.front().data;
    tx_queue_.pop_front();
    if (mode_ == Mode::kSerial) b = static_cast<uint8_t>(b | kSerialBit7);
    has_last_land_ = true;
    last_land_     = land_;
    host_.Receive(b, format);
    if (!tx_queue_.empty()) {
        if (!dtr_high_) ArmHead(last_land_);
        return;
    }
    id_in_progress_ = false;
    if (pending_ps2_) {
        mode_        = Mode::kPs2;
        baud_param_  = pending_param_;
        pending_ps2_ = false;
    }
    if (sample_due_) {
        sample_due_ = false;
        TakeReport();
    }
}

uint8_t IbmTrackpoint::ReadRam(uint8_t addr) {
    if (ram_known_[addr] == 0u) host_.Halt("RAM read", addr);
    return ram_[addr];
}

void IbmTrackpoint::SaveState(StateWriter& w) {
    const int64_t        now = Now();
    std::vector<uint8_t> queued;
    std::vector<uint8_t> first;
    for (const Output& o : tx_queue_) {
        queued.push_back(o.data);
        first.push_back(o.first ? 1u : 0u);
    }
    w.Write("tp_mode", mode_);
    w.Write("tp_baud_param", baud_param_);
    w.Write("tp_ram", ram_);
    w.Write("tp_ram_known", ram_known_);
    w.Write("tp_parse", parse_);
    w.Write("tp_e2_command", e2_command_);
    w.Write("tp_e2_arg1", e2_arg1_);
    w.Write<uint32_t>("tp_tx_queue_size", static_cast<uint32_t>(queued.size()));
    w.WriteBytes("tp_tx_queue", queued.data(), queued.size());
    w.WriteBytes("tp_tx_first", first.data(), first.size());
    w.Write<uint8_t>("tp_tx_armed", armed_ ? 1u : 0u);
    w.Write<int64_t>("tp_tx_start_rel", start_ - now);
    w.Write<int64_t>("tp_tx_land_rel", land_ - now);
    w.Write<uint8_t>("tp_tx_has_last_land", has_last_land_ ? 1u : 0u);
    w.Write<int64_t>("tp_tx_last_land_rel", last_land_ - now);
    w.Write<int64_t>("tp_post_end_rel", post_end_ - now);
    w.Write<uint8_t>("tp_id_in_progress", id_in_progress_ ? 1u : 0u);
    w.Write<uint8_t>("tp_sample_due", sample_due_ ? 1u : 0u);
    w.Write<uint8_t>("tp_pending_ps2", pending_ps2_ ? 1u : 0u);
    w.Write("tp_pending_param", pending_param_);
    w.Write<uint8_t>("tp_dtr_high", dtr_high_ ? 1u : 0u);
    w.Write<uint8_t>("tp_rts_high", rts_high_ ? 1u : 0u);
    ps2_.SaveState(w);
    samples_.Save(w, "tp_");
}

void IbmTrackpoint::RestoreState(StateReader& r) {
    uint32_t tx_queue_size  = 0;
    uint8_t  armed          = 0;
    int64_t  start_rel      = 0;
    int64_t  land_rel       = 0;
    uint8_t  has_last_land  = 0;
    int64_t  last_land_rel  = 0;
    int64_t  post_end_rel   = 0;
    uint8_t  id_in_progress = 0;
    uint8_t  sample_due     = 0;
    uint8_t  pending_ps2    = 0;
    uint8_t  dtr_high       = 0;
    uint8_t  rts_high       = 0;
    r.Read("tp_mode", mode_);
    r.Read("tp_baud_param", baud_param_);
    r.Read("tp_ram", ram_);
    r.Read("tp_ram_known", ram_known_);
    r.Read("tp_parse", parse_);
    r.Read("tp_e2_command", e2_command_);
    r.Read("tp_e2_arg1", e2_arg1_);
    r.Read("tp_tx_queue_size", tx_queue_size);
    std::vector<uint8_t> queued(tx_queue_size);
    std::vector<uint8_t> first(tx_queue_size);
    r.ReadBytes("tp_tx_queue", queued.data(), queued.size());
    r.ReadBytes("tp_tx_first", first.data(), first.size());
    r.Read("tp_tx_armed", armed);
    r.Read("tp_tx_start_rel", start_rel);
    r.Read("tp_tx_land_rel", land_rel);
    r.Read("tp_tx_has_last_land", has_last_land);
    r.Read("tp_tx_last_land_rel", last_land_rel);
    r.Read("tp_post_end_rel", post_end_rel);
    r.Read("tp_id_in_progress", id_in_progress);
    r.Read("tp_sample_due", sample_due);
    r.Read("tp_pending_ps2", pending_ps2);
    r.Read("tp_pending_param", pending_param_);
    r.Read("tp_dtr_high", dtr_high);
    r.Read("tp_rts_high", rts_high);
    tx_queue_.clear();
    for (uint32_t i = 0; i < tx_queue_size; ++i) tx_queue_.push_back({queued[i], first[i] != 0u});
    const int64_t now = Now();
    armed_          = armed != 0u;
    start_          = now + start_rel;
    land_           = now + land_rel;
    has_last_land_  = has_last_land != 0u;
    last_land_      = now + last_land_rel;
    post_end_       = now + post_end_rel;
    id_in_progress_ = id_in_progress != 0u;
    sample_due_     = sample_due != 0u;
    pending_ps2_    = pending_ps2 != 0u;
    dtr_high_       = dtr_high != 0u;
    rts_high_       = rts_high != 0u;
    ps2_.RestoreState(r);
    samples_.Restore(r, "tp_");
    RearmTransmit();
}

}
