#include "sh77xx_scif.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../tracing/kernel_debug_sink.h"
#include "sh77xx_cpg.h"
#include "sh77xx_frqcr.h"
#include "sh77xx_pfc.h"
#include "sh77xx_serial_wiring.h"
#include "sh77xx_standby_entry.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kOffMode     = 0x0u;
constexpr uint32_t kOffBitRate  = 0x2u;
constexpr uint32_t kOffControl  = 0x4u;
constexpr uint32_t kOffTransmit = 0x6u;
constexpr uint32_t kOffStatus   = 0x8u;
constexpr uint32_t kOffReceive  = 0xAu;
constexpr uint32_t kOffFifoCtl  = 0xCu;
constexpr uint32_t kOffFifoData = 0xEu;

constexpr uint8_t kModeChr      = 0x40u;
constexpr uint8_t kModeParity   = 0x20u;
constexpr uint8_t kModeOdd      = 0x10u;
constexpr uint8_t kModeTwoStop  = 0x08u;
constexpr uint8_t kModeClock    = 0x03u;
constexpr uint8_t kModeIrda     = 0x80u;
constexpr uint8_t kModeWritable = 0x7Bu;

constexpr uint8_t kCtlTie       = 0x80u;
constexpr uint8_t kCtlRie       = 0x40u;
constexpr uint8_t kCtlTe        = 0x20u;
constexpr uint8_t kCtlRe        = 0x10u;
constexpr uint8_t kCtlUnmodeled = 0x0Fu;

constexpr uint8_t kStatusTend      = 0x40u;
constexpr uint8_t kStatusTdfe      = 0x20u;
constexpr uint8_t kStatusRdf       = 0x02u;
constexpr uint8_t kStatusDr        = 0x01u;
constexpr uint8_t kStatusClearable = 0xF3u;

constexpr uint8_t kFifoMce   = 0x08u;
constexpr uint8_t kFifoTfrst = 0x04u;
constexpr uint8_t kFifoRfrst = 0x02u;
constexpr uint8_t kFifoLoop  = 0x01u;

constexpr size_t kTxTriggers[4] = {8u, 4u, 2u, 1u};
constexpr size_t kRxTriggers[4] = {1u, 4u, 8u, 14u};

constexpr uint64_t kClocksPerBitBase = 32u;

}

Sh77xxScif::Sh77xxScif(CerfEmulator& emu)
    : Peripheral(emu),
      endpoint_timer_(emu, [this] { port_.OnTimer(); }),
      port_(*this, endpoint_timer_, [this] { return wiring_->Control(Channel().base); }) {}

Sh77xxScif::~Sh77xxScif() = default;

void Sh77xxScif::OnReady() {
    intc_   = &emu_.Get<Sh77xxIntc>();
    pfc_    = &emu_.Get<Sh77xxPfc>();
    cpg_    = &emu_.Get<Sh77xxCpg>();
    frqcr_  = &emu_.Get<Sh77xxFrqcr>();
    stbcr2_ = &emu_.Get<Sh77xxStbcr2>();
    wiring_ = emu_.TryGet<Sh77xxSerialWiring>();
    endpoint_timer_.AttachLineTimer();

    stbcr2_->RegisterChangeListener([this](uint8_t from, uint8_t to) {
        const bool stopping = (from & Channel().mstp) == 0u && (to & Channel().mstp) != 0u;
        if (stopping) Initialize();
        RefreshClockStop();
        if (stopping) FireLineConfig();
    });
    frqcr_->RegisterChangeListener([this](uint16_t, uint16_t) { FireLineConfig(); });
    pfc_->AddControlListener([this] { RefreshConnection(); });
    emu_.Get<Sh77xxStandbyEntry>().RegisterListener([this] {
        Initialize();
        FireLineConfig();
    });
    GuestCpuReset& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) { Initialize(); });
    reset.RegisterResetReleaseListener([this] {
        RefreshClockStop();
        RefreshConnection();
        FireLineConfig();
    });

    Initialize();
    RefreshClockStop();
    RefreshConnection();
    {
        std::lock_guard<std::mutex> lk(mu_);
        line_cfg_ = LineConfigLocked();
    }
    emu_.Get<PeripheralDispatcher>().Register(this);

    if (wiring_ == nullptr) return;
    const std::string label = wiring_->CradleLabel(Channel().base);
    if (label.empty()) return;
    wiring_->SetControlListener(Channel().base, [this] {
        RefreshConnection();
        port_.NotifyControlLines();
    });
    port_.AttachCradle(emu_, label);
}

void Sh77xxScif::OnShutdown() { port_.OnShutdown(); }

void Sh77xxScif::RequireRunning(const char* reg, uint32_t value, uint32_t held) const {
    if (!stopped_ || value == held) return;
    emu_.Get<Fatal>().Die("Sh77xxScif: %s write 0x%X at 0x%08X while STBCR2 stops the module clock", reg, value,
                          Channel().base);
}

uint8_t Sh77xxScif::ReadByte(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off == kOffReceive) return ReadReceiveData();
    std::lock_guard<std::mutex> lk(mu_);
    switch (off) {
        case kOffMode:    return smr_;
        case kOffBitRate: return brr_;
        case kOffControl: return scr_;
        case kOffFifoCtl: return fcr_;
        default:          HaltUnsupportedAccess("ReadByte", addr, 0u);
    }
}

void Sh77xxScif::WriteByte(uint32_t addr, uint8_t value) {
    switch (addr - MmioBase()) {
        case kOffMode:     WriteMode(value); return;
        case kOffBitRate:  WriteBitRate(value); return;
        case kOffControl:  WriteControl(value); return;
        case kOffTransmit: WriteTransmitData(value); return;
        case kOffFifoCtl:  WriteFifoControl(value); return;
        default:           HaltUnsupportedAccess("WriteByte", addr, value);
    }
}

uint16_t Sh77xxScif::ReadHalf(uint32_t addr) {
    switch (addr - MmioBase()) {
        case kOffStatus: return ReadStatus();
        case kOffFifoData: {
            std::lock_guard<std::mutex> lk(mu_);
            return static_cast<uint16_t>((tx_.size() << 8) | RxCountLocked());
        }
        default: HaltUnsupportedAccess("ReadHalf", addr, 0u);
    }
}

void Sh77xxScif::WriteHalf(uint32_t addr, uint16_t value) {
    if (addr - MmioBase() != kOffStatus) HaltUnsupportedAccess("WriteHalf", addr, value);
    WriteStatus(value);
}

void Sh77xxScif::WriteMode(uint8_t value) {
    const uint8_t stored = static_cast<uint8_t>(value & (Channel().irda ? 0xFFu : kModeWritable));
    {
        std::lock_guard<std::mutex> lk(mu_);
        RequireRunning("SCSMR", stored, smr_);
        smr_ = stored;
    }
    FireLineConfig();
}

void Sh77xxScif::WriteBitRate(uint8_t value) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        RequireRunning("SCBRR", value, brr_);
        brr_ = value;
    }
    FireLineConfig();
}

void Sh77xxScif::WriteControl(uint8_t value) {
    if ((value & kCtlUnmodeled) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxScif: SCSCR write 0x%02X at 0x%08X sets bits 3-0", value, Channel().base);
    }
    std::vector<uint8_t> out;
    bool                 tx_wired = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        RequireRunning("SCSCR", value, scr_);
        if ((scr_ & kCtlTe) != 0u && (value & kCtlTe) == 0u) tend_ = true;
        scr_ = value;
        TakeTransmitLocked(out);
        tx_wired = tx_wired_;
        DriveLocked();
    }
    DeliverTx(out, tx_wired);
}

void Sh77xxScif::WriteFifoControl(uint8_t value) {
    if ((value & (kFifoMce | kFifoLoop)) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxScif: SCFCR write 0x%02X at 0x%08X enables modem control or loopback", value,
                              Channel().base);
    }
    bool drained = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        RequireRunning("SCFCR", value, fcr_);
        fcr_ = value;
        if ((value & kFifoTfrst) != 0u) tx_.clear();
        if ((value & kFifoRfrst) != 0u) {
            drained = !RxEmptyLocked();
            rx_.clear();
            rx_pos_ = 0u;
        }
        DriveLocked();
    }
    if (drained) port_.NotifyRxDrained();
}

void Sh77xxScif::WriteStatus(uint16_t value) {
    std::lock_guard<std::mutex> lk(mu_);
    const uint8_t cleared = static_cast<uint8_t>(~value & read_ones_ & kStatusClearable);
    if (stopped_ && cleared != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxScif: SCSSR write 0x%04X at 0x%08X clears flags while STBCR2 stops the module clock",
                              value, Channel().base);
    }
    if ((cleared & kStatusTend) != 0u) tend_ = false;
    if ((cleared & kStatusTdfe) != 0u && tx_.size() > TxTriggerLocked()) tdfe_ = false;
    if ((cleared & kStatusRdf) != 0u && RxCountLocked() < RxTriggerLocked()) rdf_ = false;
    if ((cleared & kStatusDr) != 0u) dr_ = false;
    read_ones_ = static_cast<uint8_t>(read_ones_ & ~cleared);
    UpdateRxFlagsLocked();
    DriveLocked();
}

uint16_t Sh77xxScif::ReadStatus() {
    std::lock_guard<std::mutex> lk(mu_);
    const uint8_t v = static_cast<uint8_t>((tend_ ? kStatusTend : 0u) | (tdfe_ ? kStatusTdfe : 0u) |
                                           (rdf_ ? kStatusRdf : 0u) | (dr_ ? kStatusDr : 0u));
    read_ones_ = static_cast<uint8_t>(read_ones_ | v);
    return v;
}

void Sh77xxScif::WriteTransmitData(uint8_t value) {
    std::vector<uint8_t> out;
    bool                 tx_wired = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stopped_) {
            emu_.Get<Fatal>().Die("Sh77xxScif: SCFTDR write 0x%02X at 0x%08X while STBCR2 stops the module clock",
                                  value, Channel().base);
        }
        if ((fcr_ & kFifoTfrst) != 0u) {
            emu_.Get<Fatal>().Die("Sh77xxScif: SCFTDR write 0x%02X at 0x%08X while TFRST holds the transmit FIFO in "
                                  "reset",
                                  value, Channel().base);
        }
        if (tx_.size() >= kFifoBytes) return;
        tx_.push_back(value);
        tend_      = false;
        read_ones_ = static_cast<uint8_t>(read_ones_ & ~kStatusTend);
        TakeTransmitLocked(out);
        tx_wired = tx_wired_;
        DriveLocked();
    }
    DeliverTx(out, tx_wired);
}

uint8_t Sh77xxScif::ReadReceiveData() {
    uint8_t value   = 0u;
    bool    drained = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (RxEmptyLocked()) {
            emu_.Get<Fatal>().Die("Sh77xxScif: SCFRDR read at 0x%08X with the receive FIFO empty", Channel().base);
        }
        value = rx_[rx_pos_++];
        if (rx_pos_ == rx_.size()) {
            rx_.clear();
            rx_pos_ = 0u;
            drained = true;
        }
        UpdateRxFlagsLocked();
        DriveLocked();
    }
    if (drained) port_.NotifyRxDrained();
    return value;
}

bool Sh77xxScif::RxEmptyLocked() const { return rx_pos_ == rx_.size(); }

size_t Sh77xxScif::RxCountLocked() const { return std::min(rx_.size() - rx_pos_, kFifoBytes); }

size_t Sh77xxScif::TxTriggerLocked() const { return kTxTriggers[(fcr_ >> 4) & 3u]; }

size_t Sh77xxScif::RxTriggerLocked() const { return kRxTriggers[(fcr_ >> 6) & 3u]; }

void Sh77xxScif::UpdateRxFlagsLocked() {
    const size_t count = RxCountLocked();
    if (count >= RxTriggerLocked()) rdf_ = true;
    else if (count > 0u) dr_ = true;
}

void Sh77xxScif::TakeTransmitLocked(std::vector<uint8_t>& out) {
    if ((scr_ & kCtlTe) == 0u || tx_.empty()) return;
    out.swap(tx_);
    if (!IrdaModeLocked() && (smr_ & kModeChr) != 0u) {
        for (uint8_t& b : out) b = static_cast<uint8_t>(b & 0x7Fu);
    }
    tend_ = true;
    tdfe_ = true;
}

bool Sh77xxScif::IrdaModeLocked() const { return Channel().irda && (smr_ & kModeIrda) != 0u; }

void Sh77xxScif::DeliverTx(const std::vector<uint8_t>& bytes, bool tx_wired) {
    if (bytes.empty() || port_.DeliverTx(bytes.data(), bytes.size(), tx_wired)) return;
    KernelDebugSink& sink = emu_.Get<KernelDebugSink>();
    for (const uint8_t b : bytes) sink.EmitChar(static_cast<char>(b), tx_line_, Channel().name);
}

void Sh77xxScif::DriveLocked() {
    intc_->SetSourceRequest(Channel().txi, (scr_ & kCtlTie) != 0u && tdfe_);
    intc_->SetSourceRequest(Channel().rxi, (scr_ & kCtlRie) != 0u && (rdf_ || dr_));
    intc_->SetSourceRequest(Channel().eri, false);
    intc_->SetSourceRequest(Channel().bri, false);
}

void Sh77xxScif::PushRx(const uint8_t* data, size_t n) {
    if (n == 0u) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if ((scr_ & kCtlRe) == 0u || (fcr_ & kFifoRfrst) != 0u || stopped_ || !powered_) return;
        rx_.insert(rx_.end(), data, data + n);
        UpdateRxFlagsLocked();
        DriveLocked();
    }
    port_.MarkRx();
}

bool Sh77xxScif::RxEmpty() const {
    std::lock_guard<std::mutex> lk(mu_);
    return RxEmptyLocked();
}

void Sh77xxScif::SetRxDrainCallback(RxDrainFn cb) { port_.SetRxDrainCallback(std::move(cb)); }

void Sh77xxScif::SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) {
    wiring_->SetModemInputs(Channel().base, cts, dsr, ri, dcd);
}

SerialLine::LineConfig Sh77xxScif::LineConfigLocked() const {
    LineConfig c;
    if (!IrdaModeLocked()) {
        c.data_bits = (smr_ & kModeChr) != 0u ? 7u : 8u;
        if ((smr_ & kModeParity) == 0u) c.parity = LineConfig::Parity::None;
        else c.parity = (smr_ & kModeOdd) != 0u ? LineConfig::Parity::Odd : LineConfig::Parity::Even;
        c.stop = (smr_ & kModeTwoStop) != 0u ? LineConfig::Stop::Two : LineConfig::Stop::One;
    }
    const GuestCycleClock::Rate pclk   = cpg_->PeripheralRate(frqcr_->Value());
    const uint64_t              divide = kClocksPerBitBase << (2u * (smr_ & kModeClock));
    c.baud = static_cast<uint32_t>(pclk.num / (pclk.den * divide * (static_cast<uint64_t>(brr_) + 1u)));
    return c;
}

SerialLine::LineConfig Sh77xxScif::GetLineConfig() const {
    std::lock_guard<std::mutex> lk(mu_);
    return line_cfg_;
}

void Sh77xxScif::SetLineConfigCallback(LineConfigFn cb) { port_.SetLineConfigCallback(std::move(cb)); }

void Sh77xxScif::FireLineConfig() {
    LineConfig cfg;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cfg       = LineConfigLocked();
        line_cfg_ = cfg;
    }
    port_.NotifyLineConfig(cfg);
}

void Sh77xxScif::RefreshClockStop() {
    const bool stopped = stbcr2_->ModuleStopped(Channel().mstp);
    std::lock_guard<std::mutex> lk(mu_);
    stopped_ = stopped;
}

void Sh77xxScif::RefreshConnection() {
    const bool powered  = wiring_ != nullptr && wiring_->DriverPowered(Channel().base);
    const bool tx_wired = powered && pfc_->PinMode(kSh77xxPortSc, Channel().data_pin) == kSh77xxPinModeOther;
    std::lock_guard<std::mutex> lk(mu_);
    powered_  = powered;
    tx_wired_ = tx_wired;
}

void Sh77xxScif::SetEndpoint(SerialEndpoint* ep) { port_.SetEndpoint(ep); }

void Sh77xxScif::ArmEndpointTimer(int64_t delay_ns) { port_.ArmTimer(delay_ns); }

void Sh77xxScif::DisarmEndpointTimer() { port_.DisarmTimer(); }

bool Sh77xxScif::InitializeLocked() {
    const bool had_rx = !RxEmptyLocked();
    smr_       = 0x00u;
    brr_       = 0xFFu;
    scr_       = 0x00u;
    fcr_       = 0x00u;
    read_ones_ = 0u;
    tend_      = true;
    tdfe_      = true;
    rdf_       = false;
    dr_        = false;
    tx_.clear();
    rx_.clear();
    rx_pos_ = 0u;
    DriveLocked();
    return had_rx;
}

void Sh77xxScif::Initialize() {
    bool had_rx = false;
    {
        std::lock_guard<std::mutex> lk(mu_);
        had_rx = InitializeLocked();
    }
    if (had_rx) port_.NotifyRxDrained();
}

void Sh77xxScif::SaveState(StateWriter& w) {
    port_.SaveCradleState(w);
    {
        std::lock_guard<std::mutex> lk(mu_);
        w.Write("smr", smr_);
        w.Write("brr", brr_);
        w.Write("scr", scr_);
        w.Write("fcr", fcr_);
        w.Write("read_ones", read_ones_);
        w.Write("tend", tend_);
        w.Write("tdfe", tdfe_);
        w.Write("rdf", rdf_);
        w.Write("dr", dr_);
        w.Write<uint64_t>("tx_count", tx_.size());
        if (!tx_.empty()) w.WriteBytes("tx", tx_.data(), tx_.size());
        w.Write<uint64_t>("rx_count", rx_.size() - rx_pos_);
        if (!RxEmptyLocked()) w.WriteBytes("rx", rx_.data() + rx_pos_, rx_.size() - rx_pos_);
    }
    endpoint_timer_.SaveLineTimer(w, "endpoint_timer_");
    port_.SaveEndpointState(w);
}

void Sh77xxScif::RestoreState(StateReader& r) {
    port_.RestoreCradleState(r);
    {
        std::lock_guard<std::mutex> lk(mu_);
        r.Read("smr", smr_);
        r.Read("brr", brr_);
        r.Read("scr", scr_);
        r.Read("fcr", fcr_);
        r.Read("read_ones", read_ones_);
        r.Read("tend", tend_);
        r.Read("tdfe", tdfe_);
        r.Read("rdf", rdf_);
        r.Read("dr", dr_);
        uint64_t tx_count = 0u;
        r.Read("tx_count", tx_count);
        tx_.assign(static_cast<size_t>(tx_count), 0u);
        if (!tx_.empty()) r.ReadBytes("tx", tx_.data(), tx_.size());
        uint64_t rx_count = 0u;
        r.Read("rx_count", rx_count);
        rx_.assign(static_cast<size_t>(rx_count), 0u);
        rx_pos_ = 0u;
        if (!rx_.empty()) r.ReadBytes("rx", rx_.data(), rx_.size());
    }
    endpoint_timer_.RestoreLineTimer(r, "endpoint_timer_");
    port_.RestoreEndpointState(r);
}

void Sh77xxScif::PostRestore() {
    endpoint_timer_.ResumeLineTimer();
    RefreshClockStop();
    RefreshConnection();
    {
        std::lock_guard<std::mutex> lk(mu_);
        DriveLocked();
    }
    FireLineConfig();
    port_.PostRestore();
}

}
