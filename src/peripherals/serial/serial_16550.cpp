#include "serial_16550.h"

#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

#include <utility>

namespace cerfcore {

namespace {
constexpr uint32_t kRbrThrDll = 0;   /* RBR/THR, or DLL when LCR.DLAB */
constexpr uint32_t kIerDlm    = 1;   /* IER,     or DLM when LCR.DLAB */
constexpr uint32_t kIirFcr    = 2;   /* IIR (r) / FCR (w)             */
constexpr uint32_t kLcr       = 3;
constexpr uint32_t kMcr       = 4;
constexpr uint32_t kLsr       = 5;
constexpr uint32_t kMsr       = 6;
constexpr uint32_t kScr       = 7;

constexpr uint8_t IER_RDA = 0x01, IER_THR = 0x02, IER_RLS = 0x04, IER_MS = 0x08;

constexpr uint8_t IIR_NONE = 0x01;
constexpr uint8_t IIR_RLS  = 0x06, IIR_RDA = 0x04, IIR_CTI = 0x0C,
                  IIR_THRE = 0x02, IIR_MS  = 0x00;
constexpr uint8_t IIR_FIFO_BITS = 0xC0;
constexpr uint8_t kNoSource     = 0xFF;   /* sentinel (IIR_MS is 0x00) */

constexpr uint8_t FCR_ENABLE = 0x01, FCR_RCVR_RESET = 0x02;

constexpr uint8_t LCR_DLAB = 0x80;

constexpr uint8_t MCR_DTR = 0x01, MCR_RTS = 0x02, MCR_OUT1 = 0x04,
                  MCR_OUT2 = 0x08, MCR_LOOP = 0x10;

constexpr uint8_t LSR_DR = 0x01, LSR_THRE = 0x20, LSR_TEMT = 0x40;
constexpr uint8_t LSR_RLS_BITS = 0x1E;   /* OE|PE|FE|BI: generate the RLS intr */

constexpr uint8_t MSR_DCTS = 0x01, MSR_DDSR = 0x02, MSR_TERI = 0x04,
                  MSR_DDCD = 0x08;
constexpr uint8_t MSR_CTS = 0x10, MSR_DSR = 0x20, MSR_RI = 0x40, MSR_DCD = 0x80;
constexpr uint8_t MSR_DELTAS = MSR_DCTS | MSR_DDSR | MSR_TERI | MSR_DDCD;

uint8_t ModemLevels(bool cts, bool dsr, bool ri, bool dcd) {
    return static_cast<uint8_t>((cts ? MSR_CTS : 0) | (dsr ? MSR_DSR : 0) | (ri ? MSR_RI : 0) |
                                (dcd ? MSR_DCD : 0));
}

uint8_t LoopbackLevels(uint8_t mcr) {
    return static_cast<uint8_t>(((mcr & MCR_RTS) ? MSR_CTS : 0) | ((mcr & MCR_DTR) ? MSR_DSR : 0) |
                                ((mcr & MCR_OUT1) ? MSR_RI : 0) | ((mcr & MCR_OUT2) ? MSR_DCD : 0));
}

/* Backlog cap for a feeder with no flow control: a guest that stops draining otherwise
   grows the queue without limit and is later handed the whole stale burst. */
constexpr size_t kRxMax = 64u * 1024u;
}  /* namespace */

/* FCR bits 7:6 select the RX FIFO trigger; FIFO disabled triggers at 1 byte. */
size_t Serial16550::RxTriggerLocked() const {
    if (!(fcr_ & FCR_ENABLE)) return 1;
    return cfg_.rx_trigger[(fcr_ >> 6) & 3u];
}

Serial16550::Serial16550(IrqLineFn irq_line, Config cfg, Clock clock, SerialLineTimer& timer)
    : irq_line_(std::move(irq_line)), cfg_(cfg), clock_(clock), port_(*this, timer, [this] { return ControlLines(); }) {
    Reset();
}

void Serial16550::ArmEndpointTimer(int64_t delay_ns) { port_.ArmTimer(delay_ns); }

void Serial16550::DisarmEndpointTimer() { port_.DisarmTimer(); }

void Serial16550::DeliverTx(uint8_t byte) { port_.DeliverTx(&byte, 1, true); }

bool Serial16550::ControlOutputsDriven() { return true; }

void Serial16550::SetEndpoint(SerialEndpoint* endpoint) { port_.SetEndpoint(endpoint); }

SerialControlLines Serial16550::ControlLines() {
    uint8_t mcr = 0;
    {
        std::lock_guard<std::mutex> lk(mu_);
        mcr = mcr_;
    }
    const bool asserted = (mcr & MCR_LOOP) == 0u && (mcr & (MCR_DTR | MCR_RTS)) != 0u &&
                          ControlOutputsDriven();
    SerialControlLines lines;
    lines.dtr = asserted && (mcr & MCR_DTR) != 0u;
    lines.rts = asserted && (mcr & MCR_RTS) != 0u;
    return lines;
}

void Serial16550::LoadModemInputs(bool cts, bool dsr, bool ri, bool dcd) {
    std::lock_guard<std::mutex> lk(mu_);
    inputs_ = ModemLevels(cts, dsr, ri, dcd);
    if (mcr_ & MCR_LOOP) return;
    msr_ = static_cast<uint8_t>((msr_ & MSR_DELTAS) | inputs_);
}

void Serial16550::ApplyModemLevelsLocked(uint8_t levels) {
    const uint8_t old  = msr_ & 0xF0u;
    const uint8_t diff = old ^ levels;
    uint8_t d = msr_ & MSR_DELTAS;
    if (diff & MSR_CTS) d |= MSR_DCTS;
    if (diff & MSR_DSR) d |= MSR_DDSR;
    if ((old & MSR_RI) && !(levels & MSR_RI)) d |= MSR_TERI;
    if (diff & MSR_DCD) d |= MSR_DDCD;
    msr_ = static_cast<uint8_t>(levels | d);
}

void Serial16550::Reset() {
    ResetRegisters();
    port_.ResendModemInputs();
}

void Serial16550::ResetRegisters() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        ClearRegistersLocked();
        dll_ = static_cast<uint8_t>(cfg_.divisor_reset & 0xFFu);
        dlm_ = static_cast<uint8_t>(cfg_.divisor_reset >> 8);
    }
    port_.NotifyControlLines();
}

void Serial16550::MasterReset() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        ClearRegistersLocked();
    }
    port_.NotifyControlLines();
}

void Serial16550::LineReset(ResetLineKind kind) {
    if (kind == ResetLineKind::Rtc) ResetRegisters();
    else                            MasterReset();
}

void Serial16550::ClearRegistersLocked() {
    ier_ = fcr_ = lcr_ = mcr_ = lsr_ = scr_ = 0;
    msr_ = inputs_;
    thre_armed_ = false;
    rx_.clear();
    rx_pos_ = 0;
    if (last_irq_level_) { last_irq_level_ = false; irq_line_(false); }
}

Serial16550::LineConfig Serial16550::GetLineConfigLocked() const {
    LineConfig c;
    const uint32_t div = (uint32_t)dll_ | ((uint32_t)dlm_ << 8);
    c.baud = div ? (uint32_t)(clock_.source_hz / ((uint64_t)clock_.divide * 16u * div)) : 0u;
    c.data_bits = (uint8_t)(5u + (lcr_ & 0x03u));
    if (!(lcr_ & 0x08u)) {
        c.parity = LineConfig::Parity::None;        /* parity disabled */
    } else {
        switch (lcr_ & 0x38u) {
            case 0x08u: c.parity = LineConfig::Parity::Odd;   break;
            case 0x18u: c.parity = LineConfig::Parity::Even;  break;
            case 0x28u: c.parity = LineConfig::Parity::Mark;  break;
            default:    c.parity = LineConfig::Parity::Space; break;   /* 0x38 */
        }
    }
    if (!(lcr_ & 0x04u))      c.stop = LineConfig::Stop::One;
    else if (c.data_bits == 5) c.stop = LineConfig::Stop::OnePointFive;
    else                       c.stop = LineConfig::Stop::Two;
    return c;
}

Serial16550::LineConfig Serial16550::GetLineConfig() const {
    std::lock_guard<std::mutex> lk(mu_);
    return GetLineConfigLocked();
}

void Serial16550::SetLineConfigCallback(LineConfigFn cb) { port_.SetLineConfigCallback(std::move(cb)); }

/* Highest-priority enabled pending source, or kNoSource. The character-timeout source
   is FIFO-mode only and carries its own enable on parts that separate it from RDA. */
uint8_t Serial16550::PendingSourceLocked() const {
    const size_t rx_avail = RxAvailLocked();
    if ((ier_ & IER_RLS) && (lsr_ & LSR_RLS_BITS)) return IIR_RLS;
    if ((ier_ & IER_RDA) && rx_avail >= RxTriggerLocked()) return IIR_RDA;
    if ((ier_ & cfg_.cti_ier_bit) && (fcr_ & FCR_ENABLE) && rx_avail > 0) return IIR_CTI;
    if ((ier_ & IER_THR) && thre_armed_) return IIR_THRE;
    if ((ier_ & IER_MS) && (msr_ & MSR_DELTAS)) return IIR_MS;
    return kNoSource;
}

/* PC16550D §8.6.6: "Disabling an interrupt prevents it from being indicated as active in
   the IIR and from activating the INTR output signal." */
bool Serial16550::ComputeIrqLevelLocked() const {
    const bool ungated = cfg_.loopback_ungates_irq && (mcr_ & MCR_LOOP) != 0u;
    if (!ungated && (mcr_ & cfg_.irq_gate_mcr) != cfg_.irq_gate_mcr) return false;
    if ((ier_ & cfg_.irq_gate_ier) != cfg_.irq_gate_ier) return false;
    return PendingSourceLocked() != kNoSource;
}

void Serial16550::SettleAndFireIrq() {
    const bool level = ComputeIrqLevelLocked();
    if (level != last_irq_level_) {
        last_irq_level_ = level;
        irq_line_(level);   /* safe under mu_: the slot IRQ path never re-enters
                               this UART, and serializing the call here prevents
                               two threads racing stale levels onto the line. */
    }
}

uint8_t Serial16550::ReadIirLocked() {
    const uint8_t src  = PendingSourceLocked();
    const uint8_t fifo = (fcr_ & FCR_ENABLE) ? IIR_FIFO_BITS : 0;
    if (src == kNoSource) return IIR_NONE | fifo;
    if (src == IIR_THRE) thre_armed_ = false;   /* IIR read clears THRE source */
    return src | fifo;
}

uint8_t Serial16550::ReadReg8(uint32_t offset) {
    uint8_t result = 0xFF;
    bool drained = false;
    {
    std::lock_guard<std::mutex> lk(mu_);
    switch (offset & 7u) {
        case kRbrThrDll:
            if (lcr_ & LCR_DLAB) { result = dll_; break; }
            if (RxAvailLocked() > 0) {
                result = rx_[rx_pos_++];
                if (rx_pos_ == rx_.size()) { rx_.clear(); rx_pos_ = 0; drained = true; }
            } else {
                result = 0;
            }
            break;
        case kIerDlm: result = (lcr_ & LCR_DLAB) ? dlm_ : ier_; break;
        case kIirFcr: result = ReadIirLocked(); break;
        case kLcr:    result = lcr_; break;
        case kMcr:    result = mcr_; break;
        case kLsr:
            /* TX is drained instantly, so THRE+TEMT always read set. */
            result = (uint8_t)(lsr_ | LSR_THRE | LSR_TEMT);
            if (RxAvailLocked() > 0) result |= LSR_DR;
            lsr_ &= (uint8_t)~LSR_RLS_BITS;     /* reading LSR clears error/break */
            break;
        case kMsr:
            result = msr_;
            msr_ &= (uint8_t)~MSR_DELTAS;       /* reading MSR clears delta bits  */
            break;
        case kScr: result = scr_; break;
    }
    SettleAndFireIrq();
    }
    if (drained) port_.NotifyRxDrained();
    return result;
}

void Serial16550::WriteReg8(uint32_t offset, uint8_t value) {
    uint8_t tx_byte = 0;
    bool do_tx = false, loop_tx = false, notify_lines = false, line_changed = false;
    LineConfig   line_cfg;
    {
        std::lock_guard<std::mutex> lk(mu_);
        switch (offset & 7u) {
            case kRbrThrDll:
                if (lcr_ & LCR_DLAB) { dll_ = value; line_changed = true; break; }
                /* THR write: byte goes out, THR re-empties -> THRE re-arms. */
                thre_armed_ = true;
                tx_byte = value;
                if (mcr_ & MCR_LOOP) loop_tx = true; else do_tx = true;
                break;
            case kIerDlm:
                if (lcr_ & LCR_DLAB) { dlm_ = value; line_changed = true; break; }
                /* PC16550D §8.6.3 bit 5: THRE "causes the UART to issue an interrupt to
                   the CPU when the [THRE] Interrupt enable is set high", so a driver that
                   starts transmitting by arming TIE gets that first interrupt. */
                if (!(ier_ & IER_THR) && (value & IER_THR)) thre_armed_ = true;
                ier_ = value & cfg_.ier_mask;
                break;
            case kIirFcr:
                fcr_ = value;
                if (value & FCR_RCVR_RESET) { rx_.clear(); rx_pos_ = 0; }
                break;
            case kLcr: lcr_ = value; line_changed = true; break;
            case kMcr: {
                const uint8_t old = mcr_;
                mcr_ = value & 0x1Fu;
                ApplyModemLevelsLocked((mcr_ & MCR_LOOP) ? LoopbackLevels(mcr_) : inputs_);
                notify_lines = ((old ^ mcr_) & (MCR_DTR | MCR_RTS | MCR_LOOP)) != 0u;
                break;
            }
            case kLsr: case kMsr: break;   /* read-only */
            case kScr: scr_ = value; break;
        }
        SettleAndFireIrq();
        if (line_changed) line_cfg = GetLineConfigLocked();
    }
    if (do_tx) DeliverTx(tx_byte);
    if (loop_tx) Serial16550::PushRx(&tx_byte, 1);
    if (notify_lines) port_.NotifyControlLines();
    if (line_changed) port_.NotifyLineConfig(line_cfg);
}

bool Serial16550::RxEmpty() const {
    std::lock_guard<std::mutex> lk(mu_);
    return RxAvailLocked() == 0;
}

void Serial16550::SetRxDrainCallback(RxDrainFn cb) { port_.SetRxDrainCallback(std::move(cb)); }

void Serial16550::PushRx(const uint8_t* data, size_t n) {
    if (n == 0) return;
    port_.MarkRx();
    std::lock_guard<std::mutex> lk(mu_);
    if (rx_pos_ == rx_.size()) { rx_.clear(); rx_pos_ = 0; }
    else if (rx_pos_ > 4096) { rx_.erase(rx_.begin(), rx_.begin() + rx_pos_); rx_pos_ = 0; }
    if (RxAvailLocked() + n > kRxMax) {
        LOG(Caution, "[16550] RX overrun: the guest is not draining the receiver, "
                     "dropping %zu bytes\n", n);
        return;
    }
    rx_.insert(rx_.end(), data, data + n);
    SettleAndFireIrq();
}

void Serial16550::SaveState(StateWriter& w) const {
    {
        std::lock_guard<std::mutex> lk(mu_);
        w.Write("ier", ier_); w.Write("fcr", fcr_); w.Write("lcr", lcr_); w.Write("mcr", mcr_);
        w.Write("lsr", lsr_); w.Write("msr", msr_); w.Write("scr", scr_);
        w.Write("dll", dll_); w.Write("dlm", dlm_);
        w.Write("modem_inputs", inputs_);
        w.Write<uint8_t>("thre_armed", thre_armed_ ? 1u : 0u);
        w.Write<uint64_t>("rx_count", rx_.size());
        if (!rx_.empty()) w.WriteBytes("rx", rx_.data(), rx_.size());
        w.Write<uint64_t>("rx_pos", rx_pos_);
    }
    port_.SaveEndpointState(w);
}

void Serial16550::RestoreState(StateReader& r) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        r.Read("ier", ier_); r.Read("fcr", fcr_); r.Read("lcr", lcr_); r.Read("mcr", mcr_);
        r.Read("lsr", lsr_); r.Read("msr", msr_); r.Read("scr", scr_);
        r.Read("dll", dll_); r.Read("dlm", dlm_);
        r.Read("modem_inputs", inputs_);
        uint8_t armed = 0; r.Read("thre_armed", armed); thre_armed_ = (armed != 0);
        uint64_t n = 0; r.Read("rx_count", n);
        rx_.assign(static_cast<size_t>(n), 0u);
        if (n) r.ReadBytes("rx", rx_.data(), static_cast<size_t>(n));
        uint64_t pos = 0; r.Read("rx_pos", pos);
        rx_pos_ = static_cast<size_t>(pos);
    }
    port_.RestoreEndpointState(r);
}

void Serial16550::PostRestoreLine() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        last_irq_level_ = ComputeIrqLevelLocked();
        irq_line_(last_irq_level_);
    }
    port_.PostRestore();
}

void Serial16550::SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) {
    std::lock_guard<std::mutex> lk(mu_);
    inputs_ = ModemLevels(cts, dsr, ri, dcd);
    if (mcr_ & MCR_LOOP) return;
    ApplyModemLevelsLocked(inputs_);
    SettleAndFireIrq();
}

}
