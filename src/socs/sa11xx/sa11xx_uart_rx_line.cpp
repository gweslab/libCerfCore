#include "sa11xx_uart_rx_line.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sa1110_id.h"
#include "sa1100_id.h"
#include <cerfcore/state/state_stream.h>
#include "sa11xx_uart_base.h"

#include <vector>

namespace cerfcore {

using namespace sa11xx_uart;

bool Sa11xxUartRxLine::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && (bd->GetSocId() == SocId::Sa1110 || bd->GetSocId() == SocId::Sa1100);
}

void Sa11xxUartRxLine::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
}

uint32_t Sa11xxUartRxLine::Attach(Sa11xxUartBase* owner, const char* name) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Sa11xxUartRxLine: a fourth UART attached");
    const uint32_t index = count_++;
    line_[index] = std::make_unique<Line>(owner, name);
    return index;
}

bool Sa11xxUartRxLine::Runs(uint32_t port, const Control& ctl) const {
    GuestCycleClock::Rate bit{};
    return line_[port]->owner->BitClock(ctl, bit);
}

bool Sa11xxUartRxLine::Push(uint32_t port, const uint8_t* data, size_t n) {
    return line_[port]->rx.Push(data, n);
}

/* §11.11.1.1 (printed 11-110): "the receiver only tests for one stop bit per frame". */
UartRxLine::Frame Sa11xxUartRxLine::FrameOf(const Control& ctl) {
    const uint64_t f   = FrameBits(ctl.utcr0);
    const uint64_t one = f - ((ctl.utcr0 & kUtcr0Sbs) != 0u ? 1u : 0u);
    return UartRxLine::Frame{2u * one - 1u, 2u * f};
}

void Sa11xxUartRxLine::RateOverflow(const Line& l) {
    emu_.Get<Fatal>().Die("%s: baud rate overflows the cycle clock ratio", l.rx.Name());
}

/* §11.11.1.2 (printed 11-110): "The receive baud clock is synchronized with the data stream using a
   digital PLL each time the start bit is detected"; "Receive data is then sampled halfway through
   each bit period". */
void Sa11xxUartRxLine::StartClock(Line& l, const Control& ctl, uint64_t now) {
    GuestCycleClock::Rate bit{};
    l.owner->BitClock(ctl, bit);
    if (!l.rx.StartClock(clock_->ClockRate(), bit, now)) RateOverflow(l);
}

void Sa11xxUartRxLine::Accept(uint32_t port, const Control& ctl, uint64_t now) {
    std::vector<uint8_t> bytes = line_[port]->rx.TakeWire();
    if (bytes.empty()) return;
    Receive(port, ctl, bytes.data(), bytes.size(), now);
}

/* §11.11.5.6 (printed 11-117): with LBM=1 "control of the TXD3 and RXD3 pins is given to the
   peripheral pin control (PPC) unit". */
void Sa11xxUartRxLine::Receive(uint32_t port, const Control& ctl, const uint8_t* data, size_t n,
                               uint64_t now) {
    Line& l = *line_[port];
    if ((ctl.utcr3 & kUtcr3Rxe) == 0u) return;
    if ((ctl.utcr3 & kUtcr3Lbm) != 0u) {
        LogLoopback(l, n);
        return;
    }
    GuestCycleClock::Rate bit{};
    if (!l.owner->BitClock(ctl, bit)) {
        l.rx.LogNoClock(n);
        return;
    }
    if (!l.rx.Receive(data, n, clock_->ClockRate(), bit, FrameOf(ctl), now)) RateOverflow(l);
}

/* §11.11.5.6 (printed 11-117): "When LBM=1, ... control of the TXD3 and RXD3 pins is given to the
   peripheral pin control (PPC) unit". */
void Sa11xxUartRxLine::LogLoopback(const Line& l, size_t n) {
    LOG(Caution, "[UART] %s RX: loopback mode, %zu bytes on the receive pin not latched\n",
        l.rx.Name(), n);
}

/* §11.11.5.6 (printed 11-117): "When LBM=1, the output of the transmit serial shifter is directly
   connected to the input of the receive serial shifter internally". */
void Sa11xxUartRxLine::Loop(uint32_t port, uint8_t byte, const Control& ctl, uint64_t now) {
    if ((ctl.utcr3 & kUtcr3Rxe) == 0u || !Runs(port, ctl)) return;
    Line& l = *line_[port];
    StartClock(l, ctl, now);
    l.rx.Append(byte, 0u);
}

bool Sa11xxUartRxLine::Break(uint32_t port, bool low, const Control& ctl, uint64_t now) {
    if ((ctl.utcr3 & kUtcr3Rxe) == 0u || !Runs(port, ctl)) return false;
    Line& l = *line_[port];
    StartClock(l, ctl, now);
    l.rx.Append(low ? kBreakBegin : kBreakEnd, 0u);
    return true;
}

void Sa11xxUartRxLine::Rescale(uint32_t port, uint64_t now) {
    Line& l = *line_[port];
    if (!l.rx.Rescale(clock_->ClockRate(), now)) RateOverflow(l);
}

bool Sa11xxUartRxLine::Retime(uint32_t port, const Control& old_ctl, const Control& ctl,
                              uint64_t now) {
    Line& l = *line_[port];
    if (!l.rx.Timed()) return false;
    bool dropped = false;
    if ((ctl.utcr3 & ~old_ctl.utcr3 & kUtcr3Lbm) != 0u && l.rx.Pending() != 0u) {
        LogLoopback(l, l.rx.Pending());
        l.rx.Clear();
        dropped = true;
    }
    GuestCycleClock::Rate bit{};
    if (!l.owner->BitClock(ctl, bit)) return l.rx.DropNoClock() || dropped;
    const bool reframed = l.rx.Reframe(FrameOf(old_ctl), FrameOf(ctl));
    bool       rerated  = false;
    if (!l.rx.SetBitRate(clock_->ClockRate(), bit, now, rerated)) RateOverflow(l);
    return dropped || reframed || rerated;
}

void Sa11xxUartRxLine::Stop(uint32_t port) {
    line_[port]->rx.Stop();
}

void Sa11xxUartRxLine::Reset(uint32_t port) {
    line_[port]->rx.Reset();
}

void Sa11xxUartRxLine::Save(uint32_t port, StateWriter& w) {
    line_[port]->rx.Save(w, clock_->Cycles());
}

void Sa11xxUartRxLine::Restore(uint32_t port, StateReader& r) {
    line_[port]->rx.Restore(r, clock_->ClockRate(), clock_->Cycles());
}

REGISTER_SERVICE(Sa11xxUartRxLine);

}
