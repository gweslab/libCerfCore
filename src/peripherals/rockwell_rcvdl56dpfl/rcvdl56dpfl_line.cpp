#include "rcvdl56dpfl_line.h"

#include "rcvdl56dpfl_bit_tick.h"
#include "rcvdl56dpfl_dsp_ram.h"
#include "rcvdl56dpfl_registers.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

namespace {

using namespace rcvdl56dpfl_reg;

constexpr uint8_t kRxStatus = Rcvdl56dpflReceiver::kStatusSyncd | Rcvdl56dpflReceiver::kStatusFe |
                              Rcvdl56dpflReceiver::kStatusPe;

constexpr uint8_t kAbcodeTxAbort   = 0x41u;
constexpr uint8_t kAbcodeCleardown = 0x96u;

constexpr uint8_t kWaitTdbia = 0x01u;
constexpr uint8_t kWaitRdbia = 0x02u;
constexpr uint8_t kWaitNsia  = 0x04u;

constexpr uint32_t kLineStatusRegisters[] = {kStatus01, kStatus0A, kStatus0C, kStatus0D, kStatus0F, kDownload};

uint8_t WithBits(uint8_t value, uint8_t bits, bool set) {
    return set ? static_cast<uint8_t>(value | bits) : static_cast<uint8_t>(value & ~bits);
}

}

void Rcvdl56dpflLine::Reset(const Registers& defaults) {
    regs_         = defaults;
    active_conf_  = regs_[kConf];
    loop3_        = false;
    v21_sync_     = false;
    cleared_down_ = false;
    DisconnectRemote();
    transmitter_.Reset();
    receiver_.Reset();
}

void Rcvdl56dpflLine::ConnectRemote(uint8_t conf, uint8_t speed) {
    active_conf_ = conf;
    far_end_.Connect(now_);
    SetStatus(kConf, conf);
    SetStatus(kStatus0E, static_cast<uint8_t>((regs_[kStatus0E] & ~kSpeedMask) | speed));
}

void Rcvdl56dpflLine::DisconnectRemote() {
    far_end_.Disconnect();
}

void Rcvdl56dpflLine::ReportCleardown(const Rcvdl56dpflCleardown& cleardown) {
    cleared_down_ = true;
    SetStatus(kConf, cleardown.conf);
    if (cleardown.abort_code) SetStatus(kDownload, kAbcodeCleardown);
}

void Rcvdl56dpflLine::QueueRemoteBytes(const std::vector<uint8_t>& bytes) {
    far_end_.Queue(LineConfig(), bytes, now_);
}

void Rcvdl56dpflLine::DeliverFarEnd(uint64_t tick) {
    const Rcvdl56dpflLineConfig c = LineConfig();
    bool overrun = false;
    bool v54     = false;
    receiver_.Accept(c, far_end_.Deliver(c, tick), now_, &overrun, &v54);
    if (overrun) SetStatus(kStatus0A, WithBits(regs_[kStatus0A], kOe, true));
    if (v54) SetStatus(kStatus0F, WithBits(regs_[kStatus0F], kV54dt, true));
}

void Rcvdl56dpflLine::LatchConfiguration() {
    active_conf_  = regs_[kConf];
    loop3_        = (regs_[kLoopback] & kL3act) != 0u;
    v21_sync_     = (regs_[kTransmit] & kV21s) != 0u;
    cleared_down_ = false;
}

Rcvdl56dpflLineConfig Rcvdl56dpflLine::LineConfig() const {
    Rcvdl56dpflLineConfig c = Rcvdl56dpflConfiguration::Line(regs_, *ram_, active_conf_, InternalLoop(), v21_sync_);
    c.far_end_listens       = far_end_.Connected();
    return c;
}

bool Rcvdl56dpflLine::InternalLoop() const {
    return loop3_ && !(regs_[kPower] & kExl3);
}

bool Rcvdl56dpflLine::CarrierWanted() const {
    if (cleared_down_ || Rcvdl56dpflConfiguration::BitRate(active_conf_) == 0u) return false;
    const bool data = (regs_[kMode] & kData) != 0u;
    const bool dtr  = (regs_[kMode] & kDtr) != 0u;
    const bool rts  = (regs_[kTransmit] & kRts) != 0u;
    if (Rcvdl56dpflConfiguration::Fax(active_conf_)) return rts && data;
    if (Rcvdl56dpflConfiguration::FskDataState(active_conf_)) return data && dtr;
    return (loop3_ || far_end_.Connected()) && data && dtr;
}

uint64_t Rcvdl56dpflLine::NextEventTick() const {
    const uint64_t far_end = far_end_.NextTick(LineConfig(), now_);
    return std::min({transmitter_.EventTick(), receiver_.EventTick(), far_end});
}

void Rcvdl56dpflLine::CatchUp(uint64_t elapsed, uint64_t now) {
    for (uint64_t tick = NextEventTick(); tick <= elapsed; tick = NextEventTick()) {
        now_ = tick;
        ProcessDueEvent();
    }
    now_ = now;
}

void Rcvdl56dpflLine::ProcessDueEvent() {
    const uint64_t far_end = far_end_.NextTick(LineConfig(), now_);
    if (far_end != kRcvdl56dpflNoEvent && far_end <= transmitter_.EventTick() && far_end <= receiver_.EventTick()) {
        DeliverFarEnd(far_end);
    } else if (transmitter_.EventTick() <= receiver_.EventTick()) {
        const Rcvdl56dpflLineConfig c = LineConfig();
        Rcvdl56dpflTxOut out;
        transmitter_.EventDue(c, &out);
        ApplyTxOut(c, out);
        if (out.pattern_done) regs_[kTransmit] = WithBits(regs_[kTransmit], out.pattern_done, false);
    } else {
        receiver_.IdleTimeout();
    }
    RefreshLine();
}

void Rcvdl56dpflLine::RefreshLine() {
    UpdateClearToSend();
    if (RateChangeDue(kTransmit, kRtrn, false)) CompleteRateChange(kTransmit, kRtrn, false);
    if (RateChangeDue(kPower, kRren, true)) CompleteRateChange(kPower, kRren, true);
    const Rcvdl56dpflLineConfig c = LineConfig();
    Rcvdl56dpflTxOut out;
    if (CarrierWanted()) {
        if (transmitter_.Carrier())
            transmitter_.Reconfigure(c, now_, &out);
        else
            transmitter_.CarrierOn(c, now_, &out);
    } else if (transmitter_.Carrier()) {
        transmitter_.CarrierOff();
        receiver_.LostCarrier();
    }
    ApplyTxOut(c, out);
    PublishLineStatus();
}

const char* Rcvdl56dpflLine::QueueTransmitByte(uint8_t value) {
    const Rcvdl56dpflLineConfig c = LineConfig();
    if (c.tx_fifo && transmitter_.Full(c)) return "TBUFFER write while the transmit FIFO is full (TXFNF clear)";
    transmitter_.Load(c, value, c.tx_fifo && (regs_[kControl11] & kTeof), (regs_[kControl11] & kTxp) != 0u);
    return nullptr;
}

void Rcvdl56dpflLine::SetTransmitBufferEmpty(bool empty) {
    regs_[kBuffer] = WithBits(regs_[kBuffer], kTdbe, empty);
    UpdateBufferInterrupts();
}

void Rcvdl56dpflLine::HostReadReceiveBuffer() {
    if (receiver_.Empty()) return;
    receiver_.HostRead(LineConfig(), now_);
    PublishLineStatus();
}

bool Rcvdl56dpflLine::RateChangeDue(uint32_t reg, uint8_t bit, bool renegotiation) const {
    if (!(regs_[reg] & bit) || !CarrierWanted() || !(regs_[kStatus0F] & kCts)) return false;
    return renegotiation ? Rcvdl56dpflConfiguration::RenegotiatesRate(active_conf_)
                         : Rcvdl56dpflConfiguration::Retrains(active_conf_);
}

void Rcvdl56dpflLine::CompleteRateChange(uint32_t reg, uint8_t bit, bool renegotiation) {
    regs_[reg] = WithBits(regs_[reg], bit, false);
    const uint8_t requested = regs_[kConf];
    if (!(regs_[kPower] & kEarc) && requested != active_conf_) {
        if (Rcvdl56dpflConfiguration::SameRetrainMode(active_conf_, requested) &&
            Rcvdl56dpflConfiguration::BitRate(requested) != 0u) {
            active_conf_ = requested;
        } else {
            const Rcvdl56dpflCleardown cleardown = Rcvdl56dpflConfiguration::CleardownOf(active_conf_);
            if (cleardown.documented) ReportCleardown(cleardown);
        }
    }
    if (!renegotiation && !cleared_down_) {
        const uint8_t speed = Rcvdl56dpflConfiguration::SpeedCode(Rcvdl56dpflConfiguration::BitRate(active_conf_));
        SetStatus(kStatus0E, static_cast<uint8_t>((regs_[kStatus0E] & ~kSpeedMask) | speed));
    }
    UpdateClearToSend();
}

void Rcvdl56dpflLine::ApplyTxOut(const Rcvdl56dpflLineConfig& c, const Rcvdl56dpflTxOut& out) {
    bool overrun = false;
    bool v54     = false;
    if (c.loop_listens)
        for (uint8_t i = 0; i < out.sent_count; ++i) receiver_.Accept(c, out.sent[i], out.tick, &overrun, &v54);
    for (uint8_t i = 0; i < out.sent_count; ++i) far_end_.FromGuest(c, out.sent[i]);
    if (overrun) SetStatus(kStatus0A, WithBits(regs_[kStatus0A], kOe, true));
    if (v54) SetStatus(kStatus0F, WithBits(regs_[kStatus0F], kV54dt, true));
    if (out.underrun_abort) SetStatus(kDownload, kAbcodeTxAbort);
}

void Rcvdl56dpflLine::PublishLineStatus() {
    const Rcvdl56dpflLineConfig c = LineConfig();
    const bool    rx_data = !receiver_.Empty();
    const uint8_t front   = rx_data ? receiver_.FrontStatus() : 0u;
    uint8_t status = static_cast<uint8_t>(regs_[kStatus0A] & ~(kRxStatus | kFlags | kCrcs));
    if (rx_data) status |= static_cast<uint8_t>(front & kRxStatus);
    else if (c.hdlc && receiver_.FlagsDetected()) status |= Rcvdl56dpflReceiver::kStatusSyncd;
    if (transmitter_.Carrier()) {
        status = WithBits(status, kFlags, transmitter_.SendingFlags(c));
        status = WithBits(status, kCrcs, transmitter_.SendingFcs());
    }
    SetStatus(kStatus0A, status);
    uint8_t fifo = static_cast<uint8_t>(regs_[kStatus01] & ~(kTxhf | kRxhf | kRxp));
    fifo = WithBits(fifo, kTxhf, c.tx_fifo && transmitter_.Txhf());
    fifo = WithBits(fifo, kRxhf, receiver_.Rxhf());
    fifo = WithBits(fifo, kRxp, (front & Rcvdl56dpflReceiver::kStatusRxp) != 0u);
    SetStatus(kStatus01, fifo);
    SetStatus(kStatus0C, WithBits(regs_[kStatus0C], kRxfne, rx_data));
    SetStatus(kStatus0D, WithBits(regs_[kStatus0D], kTxfnf, transmitter_.Txfnf(c)));
    if (rx_data) regs_[kRbuffer] = receiver_.Front();
    uint8_t buffer = WithBits(regs_[kBuffer], kTdbe, transmitter_.Tdbe(c));
    regs_[kBuffer] = WithBits(buffer, kRdbf, receiver_.Rdbf());
    UpdateBufferInterrupts();
}

void Rcvdl56dpflLine::SetStatus(uint32_t reg, uint8_t value) {
    const uint8_t changed = static_cast<uint8_t>(regs_[reg] ^ value);
    regs_[reg] = value;
    if (changed & ram_->StatusChangeMask(reg)) NoteNewStatus();
}

void Rcvdl56dpflLine::NoteNewStatus() {
    regs_[kNewStatus] |= kNews;
    UpdateNewStatusInterrupt();
}

void Rcvdl56dpflLine::UpdateClearToSend() {
    const bool fax = Rcvdl56dpflConfiguration::Fax(active_conf_);
    const bool fsk = Rcvdl56dpflConfiguration::FskDataState(active_conf_);
    const bool rts_data = (regs_[kTransmit] & kRts) && (regs_[kMode] & kData);
    const bool dtr = (regs_[kMode] & kDtr) != 0u;
    const bool remote_loop = Rcvdl56dpflConfiguration::RemoteLoopRequested(regs_, active_conf_);
    const bool cts = rts_data && CarrierWanted() &&
                     (fax || (fsk && dtr) || ((loop3_ || far_end_.Connected()) && dtr)) && !remote_loop;
    SetStatus(kStatus0F, WithBits(regs_[kStatus0F], kCts, cts));
    UpdateLoopStatus();
}

void Rcvdl56dpflLine::UpdateLoopStatus() {
    const bool dtr    = (regs_[kMode] & kDtr) != 0u;
    const bool looped = InternalLoop() && CarrierWanted() && !(regs_[kControl05] & kTxsq);
    const bool remote = far_end_.Connected();
    const bool energy = looped || (remote && dtr);
    uint8_t status = WithBits(regs_[kStatus0F], kDsr, (loop3_ || remote) && dtr);
    status = WithBits(status, kFed, energy);
    SetStatus(kStatus0F, WithBits(status, kRlsd, energy));
}

void Rcvdl56dpflLine::UpdateNewStatusInterrupt() {
    uint8_t& status = regs_[kNewStatus];
    if ((status & kNews) && (status & kNsie))
        status |= kNsia;
    else
        status &= static_cast<uint8_t>(~kNsia);
}

void Rcvdl56dpflLine::UpdateBufferInterrupts() {
    uint8_t buffer = static_cast<uint8_t>(regs_[kBuffer] & ~(kTdbia | kRdbia));
    if ((buffer & kTdbie) && (buffer & kTdbe)) buffer |= kTdbia;
    if ((buffer & kRdbie) && (buffer & kRdbf)) buffer |= kRdbia;
    regs_[kBuffer] = buffer;
}

bool Rcvdl56dpflLine::IrqAsserted() const {
    return (regs_[kNewStatus] & (kNsia | kNcia)) != 0u || (regs_[kBuffer] & (kTdbia | kRdbia)) != 0u;
}

uint8_t Rcvdl56dpflLine::WaitingInterrupts() const {
    uint8_t waiting = 0u;
    const uint8_t buffer = regs_[kBuffer];
    if ((buffer & kTdbie) && !(buffer & kTdbia)) waiting |= kWaitTdbia;
    if ((buffer & kRdbie) && !(buffer & kRdbia)) waiting |= kWaitRdbia;
    const uint8_t news = regs_[kNewStatus];
    if ((news & kNsie) && !(news & kNsia))
        for (uint32_t reg : kLineStatusRegisters)
            if (ram_->StatusChangeMask(reg) != 0u) waiting |= kWaitNsia;
    return waiting;
}

uint8_t Rcvdl56dpflLine::RaisedInterrupts() const {
    uint8_t raised = 0u;
    if (regs_[kBuffer] & kTdbia) raised |= kWaitTdbia;
    if (regs_[kBuffer] & kRdbia) raised |= kWaitRdbia;
    if (regs_[kNewStatus] & kNsia) raised |= kWaitNsia;
    return raised;
}

bool Rcvdl56dpflLine::SameStateAs(const Rcvdl56dpflLine& other) const {
    return regs_ == other.regs_ && active_conf_ == other.active_conf_ && loop3_ == other.loop3_ &&
           far_end_.SameStateExceptDueTick(other.far_end_) && v21_sync_ == other.v21_sync_ && cleared_down_ == other.cleared_down_ && receiver_ == other.receiver_ &&
           transmitter_.SameStateExceptEventTick(other.transmitter_);
}

uint64_t Rcvdl56dpflLine::InterruptRiseTick() const {
    const uint8_t waiting = WaitingInterrupts();
    if (waiting == 0u) return kRcvdl56dpflNoEvent;
    Rcvdl56dpflLine probe  = *this;
    Rcvdl56dpflLine before = *this;
    for (uint64_t tick = probe.NextEventTick(); tick != kRcvdl56dpflNoEvent; tick = probe.NextEventTick()) {
        const bool idle_before = probe.transmitter_.Idling();
        if (idle_before) before = probe;
        probe.now_ = tick;
        probe.ProcessDueEvent();
        if (probe.RaisedInterrupts() & waiting) return tick;
        if (idle_before && probe.SameStateAs(before)) return kRcvdl56dpflNoEvent;
    }
    return kRcvdl56dpflNoEvent;
}

void Rcvdl56dpflLine::SaveState(StateWriter& w) const {
    w.Write("regs", regs_);
    w.Write("active_conf", active_conf_);
    w.Write<uint8_t>("loop3", loop3_ ? 1u : 0u);
    w.Write<uint8_t>("v21_sync", v21_sync_ ? 1u : 0u);
    w.Write<uint8_t>("cleared_down", cleared_down_ ? 1u : 0u);
    transmitter_.SaveState(w);
    receiver_.SaveState(w);
}

void Rcvdl56dpflLine::RestoreState(StateReader& r) {
    r.Read("regs", regs_);
    r.Read("active_conf", active_conf_);
    uint8_t loop3 = 0u;
    r.Read("loop3", loop3);
    loop3_ = loop3 != 0u;
    far_end_.Disconnect();
    uint8_t v21_sync = 0u;
    r.Read("v21_sync", v21_sync);
    v21_sync_ = v21_sync != 0u;
    uint8_t cleared_down = 0u;
    r.Read("cleared_down", cleared_down);
    cleared_down_ = cleared_down != 0u;
    transmitter_.RestoreState(r);
    receiver_.RestoreState(r);
}

}
