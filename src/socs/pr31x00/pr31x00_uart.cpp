#include "pr31x00_uart.h"

#include "pr31x00_clock.h"
#include "pr31x00_intc.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "pr31x00_reset_line.h"
#include <cerfcore/state/state_stream.h>
#include "../../tracing/kernel_debug_sink.h"

#include <cstdint>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kOffCtl1     = 0x00u;   /* $0B0 UARTA, $0C8 UARTB */
constexpr uint32_t kOffCtl2     = 0x04u;   /* write-only */
constexpr uint32_t kOffDmaCtl1  = 0x08u;   /* write-only */
constexpr uint32_t kOffDmaCtl2  = 0x0Cu;   /* write-only */
constexpr uint32_t kOffDmaCount = 0x10u;   /* read-only */
constexpr uint32_t kOffData     = 0x14u;   /* write TXHOLD, read RXHOLD */

constexpr uint32_t kCtl2BaudMask = 0x000003FFu;
constexpr uint64_t kClocksPerBaud = 16u;

constexpr uint32_t kDmaCtl1Reserved = 0x00000003u;
constexpr uint32_t kDmaCtl2Reserved = 0xFFFF0000u;
constexpr uint32_t kDmaLenMask      = 0x0000FFFFu;

/* Transmit Holding (§16.5.6), write-only: BREAK<8> TXDATA[7:0]. Setting BREAK with
   TXDATA $00 drives a line break until it is cleared. */
constexpr uint32_t kTxBreak        = 1u << 8;
constexpr uint32_t kTxDataMask     = 0x000000FFu;
constexpr uint32_t kTxHoldReserved = 0xFFFFFE00u;

constexpr uint32_t kCtl1Reset = (1u << 30) | (1u << 6);

constexpr uint32_t kCtl1Uarton      = 1u << 31;
constexpr uint32_t kCtl1Empty       = 1u << 30;
constexpr uint32_t kCtl1PrxHoldFull = 1u << 29;
constexpr uint32_t kCtl1RxHoldFull  = 1u << 28;
constexpr uint32_t kCtl1Reserved    = 0x0FFF0000u;
constexpr uint32_t kCtl1DisTxd   = 1u << 6;
constexpr uint32_t kCtl1EnUart   = 1u << 0;

/* TMPR3911 §16.5.1 (pages 16-11 / 16-12): ENDMARX<15> ENDMATX<14> TESTMODE<13>
   ENBREAKHALT<12> ENDMATEST<11> ENDMALOOP<10> LOOPBACK<4>. */
constexpr uint32_t kCtl1EnDmaRx   = 1u << 15;
constexpr uint32_t kCtl1EnDmaTx   = 1u << 14;
constexpr uint32_t kCtl1EnDmaLoop = 1u << 10;
constexpr uint32_t kCtl1Unmodeled = 0x00003810u;
constexpr uint32_t kCtl1Writable  = 0x0000C7EFu;

/* Frame format (§16.5.1): BIT_7<3> selects a 7-bit character, ENPARITY<1> enables
   parity with EVENPARITY<2> selecting even, TWOSTOP<5> selects two stop bits. */
constexpr uint32_t kCtl1Bit7       = 1u << 3;
constexpr uint32_t kCtl1EvenParity = 1u << 2;
constexpr uint32_t kCtl1EnParity   = 1u << 1;
constexpr uint32_t kCtl1TwoStop    = 1u << 5;

}  /* namespace */

bool Pr31x00Uart::ShouldRegister() {
    const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00Uart::OnReady() {
    ctl1_ = kCtl1Reset & kCtl1Writable;
    endpoint_timer_.AttachLineTimer();
    module_clock_ = &emu_.Get<Pr31x00Clock>();
    dma_      = &emu_.Get<Pr31x00UartDma>();
    dma_port_ = dma_->Attach(TxSource(),
                             [this](const Pr31x00UartDma::RxInts& i) { RaiseRxInts(i); },
                             [this] { OnRxLineIdle(); });
    tx_      = &emu_.Get<Pr31x00UartTransmitter>();
    tx_port_ = tx_->Attach(dma_port_,
                           [this](const uint8_t* data, size_t n) { DeliverTx(data, n); },
                           [this](const Pr31x00UartTransmitter::TxInts& i) { RaiseTxInts(i); });
    module_clock_->RegisterModuleClockListener([this] {
        bool moved;
        {
            std::lock_guard<std::mutex> lk(mu_);
            moved = ComputeLineConfigLocked().baud != fired_baud_;
        }
        if (moved) FireLineConfig();
        else       ApplyClockState();
    });
    line_cfg_   = ComputeLineConfigLocked();
    fired_baud_ = line_cfg_.baud;
    emu_.Get<PeripheralDispatcher>().Register(this);
    /* Control 2, DMA Control 1 / 2 and DMA Count RESET X (TMPR3911 §16.5.2-16.5.5 p16-14,
       p16-15). */
    auto& reset_line = emu_.Get<Pr31x00ResetLine>();
    reset_line.RegisterResetListener([this] {
        { std::lock_guard<std::mutex> lk(mu_); ctl1_ = kCtl1Reset & kCtl1Writable; }
        dma_->ResetLine(dma_port_);
        tx_->ResetLine(tx_port_);
    });
    reset_line.RegisterReleaseListener([this] { FireLineConfig(); });

    if (auto* wiring = emu_.TryGet<Pr31x00SerialWiring>()) modem_ = wiring->ForUart(MmioBase());
    modem_lines_ = &emu_.Get<Pr31x00UartModemLines>();
    modem_port_  = modem_lines_->Attach(modem_ ? &*modem_ : nullptr,
                                        [this](bool, bool) { port_.NotifyControlLines(); });
    if (!modem_) return;

    SetModemInputs(false, false, false, false);

    port_.AttachCradle(emu_, modem_->label);
    LOG(Periph, "[UART] %s attached as serial endpoint line '%s'\n",
        TxSource(), modem_->label.c_str());
}

Pr31x00Uart::Pr31x00Uart(CerfEmulator& emu)
    : Peripheral(emu),
      endpoint_timer_(emu, [this] { port_.OnTimer(); }),
      port_(*this, endpoint_timer_, [this] {
          SerialControlLines lines;
          modem_lines_->Control(modem_port_, lines.dtr, lines.rts);
          return lines;
      }) {}

Pr31x00Uart::~Pr31x00Uart() = default;

void Pr31x00Uart::OnShutdown() { port_.OnShutdown(); }

uint32_t Pr31x00Uart::ReadWord(uint32_t addr) {
    switch (addr - MmioBase()) {
        /* TMPR3911 §16.5.1 (pages 16-11 / 16-12): UARTON "the module will not shut down until
           the Transmit Holding Register and Transmit Shift Register are empty"; EMPTY "is high
           if the Transmit Holding Register and Transmit Shift Register are both empty". */
        case kOffCtl1: {
            uint32_t v           = 0;
            bool     rxhold_full = false, prxhold_full = false;
            dma_->ReadHolds(dma_port_, rxhold_full, prxhold_full);
            if (prxhold_full) v |= kCtl1PrxHoldFull;
            if (rxhold_full)  v |= kCtl1RxHoldFull;
            const bool tx_empty = tx_->Empty(tx_port_);
            std::lock_guard<std::mutex> lk(mu_);
            v |= ctl1_ & kCtl1Writable;
            if (tx_empty) v |= kCtl1Empty;
            if ((ctl1_ & kCtl1EnUart) || !tx_empty) v |= kCtl1Uarton;
            return v;
        }

        case kOffDmaCount: return dma_->Count(dma_port_);

        default: HaltUnsupportedAccess("PR31x00 UART ReadWord", addr, 0);
    }
}

void Pr31x00Uart::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - MmioBase()) {
        case kOffCtl1: WriteCtl1(addr, value); return;

        case kOffCtl2:
            { std::lock_guard<std::mutex> lk(mu_); ctl2_baud_ = value & kCtl2BaudMask; }
            FireLineConfig();
            return;

        case kOffDmaCtl1:
            if (value & kDmaCtl1Reserved) {
                HaltUnsupportedAccess("PR31x00 UART DMA_CTL1 reserved", addr, value);
            }
            dma_->SetBuffer(dma_port_, value);
            return;

        case kOffDmaCtl2:
            if (value & kDmaCtl2Reserved) {
                HaltUnsupportedAccess("PR31x00 UART DMA_CTL2 reserved", addr, value);
            }
            dma_->SetLength(dma_port_, (value & kDmaLenMask) + 1u);
            return;

        case kOffData: WriteTxHold(addr, value); return;

        default: HaltUnsupportedAccess("PR31x00 UART WriteWord", addr, value);
    }
}

void Pr31x00Uart::WriteCtl1(uint32_t addr, uint32_t value) {
    if (value & kCtl1Reserved) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 reserved", addr, value);
    }
    if (value & kCtl1Unmodeled) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 unmodeled control", addr, value);
    }
    if ((value & kCtl1EnDmaRx) && (value & kCtl1EnDmaTx)) {
        HaltUnsupportedAccess("PR31x00 UART CTL1 sets ENDMARX and ENDMATX", addr, value);
    }
    if ((value & kCtl1EnDmaRx) && !(value & kCtl1EnDmaLoop)) {
        HaltUnsupportedAccess("PR31x00 UART RX DMA armed without ENDMALOOP", addr, value);
    }
    if ((value & kCtl1EnDmaTx) && (value & kCtl1EnDmaLoop)) {
        HaltUnsupportedAccess("PR31x00 UART TX DMA armed with ENDMALOOP", addr, value);
    }
    bool tx_start;
    {
        std::lock_guard<std::mutex> lk(mu_);
        tx_start = (value & kCtl1EnDmaTx) != 0u && (ctl1_ & kCtl1EnDmaTx) == 0u;
        ctl1_    = value & kCtl1Writable;
    }
    if ((value & kCtl1EnDmaTx) == 0u) tx_->CancelDma(tx_port_);
    dma_->SetControl(dma_port_, (value & kCtl1EnUart) != 0u, (value & kCtl1EnDmaRx) != 0u);
    FireLineConfig();
    if (tx_start) TransmitDma(addr, value);
}

/* TMPR3911 §16.5.1 (page 16-12) ENDMATX "should not be set until the UARTDMACNTL1 and
   UARTDMACNTL2 registers are setup, the module is enabled and the empty flag is set". */
void Pr31x00Uart::TransmitDma(uint32_t addr, uint32_t control) {
    if ((control & kCtl1EnUart) == 0u || (control & kCtl1DisTxd) != 0u) {
        HaltUnsupportedAccess("PR31x00 UART TX DMA armed with ENUART clear or DISTXD set",
                              addr, control);
    }
    if (dma_->Length(dma_port_) == 0u) {
        HaltUnsupportedAccess("PR31x00 UART TX DMA armed with no DMALENGTH programmed",
                              addr, control);
    }
    tx_->StartDma(tx_port_);
}

void Pr31x00Uart::RaiseTxInts(const Pr31x00UartTransmitter::TxInts& ints) {
    uint32_t bits = 0;
    if (ints.sent)     bits |= TxEmptyIntBit() | TxAvailIntBit();
    if (ints.dma_full) bits |= DmaFullIntBit();
    if (ints.dma_half) bits |= DmaHalfIntBit();
    emu_.Get<Pr31x00Intc>().SetPending(1, bits);
}

void Pr31x00Uart::DeliverTx(const uint8_t* data, size_t n) {
    if (port_.DeliverTx(data, n, true)) return;
    auto& sink = emu_.Get<KernelDebugSink>();
    for (size_t i = 0; i < n; ++i) sink.EmitChar(static_cast<char>(data[i]), tx_line_, TxSource());
}

void Pr31x00Uart::WriteTxHold(uint32_t addr, uint32_t value) {
    if (value & kTxHoldReserved) {
        HaltUnsupportedAccess("PR31x00 UART TXHOLD reserved", addr, value);
    }
    if (value & kTxBreak) {
        HaltUnsupportedAccess("PR31x00 UART TXHOLD BREAK", addr, value);
    }

    {
        std::lock_guard<std::mutex> lk(mu_);
        if ((ctl1_ & kCtl1EnUart) == 0u || (ctl1_ & kCtl1DisTxd) != 0u) {
            return;
        }
    }

    tx_->WriteHold(tx_port_, static_cast<uint8_t>(value & kTxDataMask));
}

void Pr31x00Uart::PushRx(const uint8_t* data, size_t n) {
    if (n == 0) return;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if ((ctl1_ & kCtl1EnUart) == 0u) return;
    }
    dma_->Receive(dma_port_, data, n);
    port_.MarkRx();
}

/* The wire, not the guest's buffer: the DMA engine cannot tell how far the driver has
   read, so a feeder is free to send again once its bytes have all been clocked out. */
bool Pr31x00Uart::RxEmpty() const {
    return dma_->LineIdle(dma_port_);
}

void Pr31x00Uart::OnRxLineIdle() { port_.NotifyRxDrained(); }

void Pr31x00Uart::RaiseRxInts(const Pr31x00UartDma::RxInts& ints) {
    uint32_t bits = 0;
    if (ints.rx)       bits |= RxIntBit();
    if (ints.overrun)  bits |= RxOverrunIntBit();
    if (ints.dma_half) bits |= DmaHalfIntBit();
    if (ints.dma_full) bits |= DmaFullIntBit();
    emu_.Get<Pr31x00Intc>().SetPending(1, bits);
}

void Pr31x00Uart::SetRxDrainCallback(RxDrainFn cb) { port_.SetRxDrainCallback(std::move(cb)); }

void Pr31x00Uart::SetModemInputs(bool cts, bool dsr, bool ri, bool dcd) {
    if (modem_) modem_lines_->SetInputs(modem_port_, cts, dsr, ri, dcd);
}

SerialLine::LineConfig Pr31x00Uart::FramingLocked() const {
    LineConfig c;
    c.data_bits = (ctl1_ & kCtl1Bit7) ? 7u : 8u;
    if ((ctl1_ & kCtl1EnParity) == 0u) c.parity = LineConfig::Parity::None;
    else c.parity = (ctl1_ & kCtl1EvenParity) ? LineConfig::Parity::Even
                                              : LineConfig::Parity::Odd;
    c.stop = (ctl1_ & kCtl1TwoStop) ? LineConfig::Stop::Two : LineConfig::Stop::One;
    return c;
}

SerialLine::LineConfig Pr31x00Uart::ComputeLineConfigLocked() const {
    LineConfig c = FramingLocked();
    const GuestCycleClock::Rate uartclk = module_clock_->UartClockRate(ClockEnableBit());
    c.baud = static_cast<uint32_t>(
        uartclk.num / (uartclk.den * kClocksPerBaud * (static_cast<uint64_t>(ctl2_baud_) + 1u)));
    return c;
}

/* TMPR3911 §16.4 (page 16-9): start, 7 or 8 data bits, parity, then the transfer as the stop bit
   is clocked in; TWOSTOP sets two stop bits on the transmitter (page 16-13). */
Pr31x00UartDma::LineTiming Pr31x00Uart::FrameTimingLocked() const {
    const LineConfig c = FramingLocked();
    Pr31x00UartDma::LineTiming t;
    t.uart_clocks_per_bit = kClocksPerBaud * (static_cast<uint64_t>(ctl2_baud_) + 1u);
    t.transfer_bits = 1u + c.data_bits + (c.parity != LineConfig::Parity::None ? 1u : 0u) + 1u;
    t.frame_bits    = t.transfer_bits + (c.stop == LineConfig::Stop::Two ? 1u : 0u);
    return t;
}

Pr31x00UartDma::LineTiming Pr31x00Uart::LineTimingLocked() const {
    Pr31x00UartDma::LineTiming t = FrameTimingLocked();
    t.uart_clock = module_clock_->UartClockRate(ClockEnableBit());
    return t;
}

void Pr31x00Uart::ApplyClockState() {
    Pr31x00UartDma::LineTiming timing;
    {
        std::lock_guard<std::mutex> lk(mu_);
        timing = LineTimingLocked();
    }
    dma_->SetLine(dma_port_, timing);
    tx_->SetClockRunning(tx_port_, timing.uart_clock.num != 0u);
}

SerialLine::LineConfig Pr31x00Uart::GetLineConfig() const {
    std::lock_guard<std::mutex> lk(mu_);
    return line_cfg_;
}

void Pr31x00Uart::SetLineConfigCallback(LineConfigFn cb) { port_.SetLineConfigCallback(std::move(cb)); }

void Pr31x00Uart::FireLineConfig() {
    LineConfig cfg;
    {
        std::lock_guard<std::mutex> lk(mu_);
        cfg         = ComputeLineConfigLocked();
        fired_baud_ = cfg.baud;
        line_cfg_   = cfg;
    }
    ApplyClockState();
    port_.NotifyLineConfig(cfg);
}

void Pr31x00Uart::SetEndpoint(SerialEndpoint* ep) { port_.SetEndpoint(ep); }

void Pr31x00Uart::ArmEndpointTimer(int64_t delay_ns) { port_.ArmTimer(delay_ns); }

void Pr31x00Uart::DisarmEndpointTimer() { port_.DisarmTimer(); }

void Pr31x00Uart::SaveState(StateWriter& w) {
    port_.SaveCradleState(w);
    uint32_t ctl1_v, baud;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ctl1_v = ctl1_;
        baud   = ctl2_baud_;
    }
    w.Write("ctl1_v", ctl1_v);
    w.Write("baud", baud);
    modem_lines_->SaveState(modem_port_, w);
    dma_->SaveState(dma_port_, w);
    tx_->SaveState(tx_port_, w);
    endpoint_timer_.SaveLineTimer(w, "endpoint_timer_");
    port_.SaveEndpointState(w);
}

void Pr31x00Uart::RestoreState(StateReader& r) {
    port_.RestoreCradleState(r);
    Pr31x00UartDma::LineTiming framing;
    {
        std::lock_guard<std::mutex> lk(mu_);
        r.Read("ctl1_v", ctl1_);
        r.Read("baud", ctl2_baud_);
        framing = FrameTimingLocked();
    }
    modem_lines_->RestoreState(modem_port_, r);
    dma_->RestoreState(dma_port_, r, framing);
    tx_->RestoreState(tx_port_, r);
    endpoint_timer_.RestoreLineTimer(r, "endpoint_timer_");
    port_.RestoreEndpointState(r);
}

void Pr31x00Uart::PostRestore() {
    endpoint_timer_.ResumeLineTimer();
    {
        std::lock_guard<std::mutex> lk(mu_);
        line_cfg_   = ComputeLineConfigLocked();
        fired_baud_ = line_cfg_.baud;
    }
    ApplyClockState();
    port_.PostRestore();
}

}
