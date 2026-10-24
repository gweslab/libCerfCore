#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../uart_rx_line.h"
#include "sa11xx_uart_regs.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace cerfcore {

class Sa11xxUartBase;
class StateReader;
class StateWriter;

class Sa11xxUartRxLine : public Service {
public:
    using Control = sa11xx_uart::Control;

    static constexpr uint16_t kBreakBegin = 1u << 8;
    static constexpr uint16_t kBreakEnd   = 1u << 9;
    static constexpr uint64_t kNever      = UartRxLine::kNever;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Attach(Sa11xxUartBase* owner, const char* name);
    bool     Push(uint32_t port, const uint8_t* data, size_t n);
    void     Accept(uint32_t port, const Control& ctl, uint64_t now);
    void     Receive(uint32_t port, const Control& ctl, const uint8_t* data, size_t n,
                     uint64_t now);
    void     Loop(uint32_t port, uint8_t byte, const Control& ctl, uint64_t now);
    bool     Break(uint32_t port, bool low, const Control& ctl, uint64_t now);
    bool     Runs(uint32_t port, const Control& ctl) const;
    void     Stop(uint32_t port);
    void     Reset(uint32_t port);
    void     Rescale(uint32_t port, uint64_t now);
    bool     Retime(uint32_t port, const Control& old_ctl, const Control& ctl, uint64_t now);

    bool     FrontDue(uint32_t port, uint64_t now) const { return line_[port]->rx.FrontDue(now); }
    uint16_t Front(uint32_t port) const { return line_[port]->rx.Front(); }
    void     PopFront(uint32_t port, uint64_t now) { line_[port]->rx.PopFront(now); }
    /* §11.11.1.3 (printed 11-110): "If the receive FIFO contains valid data and three frame periods
       elapse without the reception of data on RXD3, the receiver idle interrupt is generated." */
    bool Quiet(uint32_t port, const Control& ctl, uint64_t now) {
        return line_[port]->rx.Quiet(6u * sa11xx_uart::FrameBits(ctl.utcr0), now);
    }
    uint64_t DueTick(uint32_t port, size_t index) const { return line_[port]->rx.DueTick(index); }
    uint64_t QuietTick(uint32_t port, const Control& ctl) const {
        return line_[port]->rx.QuietTick(6u * sa11xx_uart::FrameBits(ctl.utcr0));
    }
    size_t   Pending(uint32_t port) const { return line_[port]->rx.Pending(); }
    uint64_t CycleOfTick(uint32_t port, uint64_t tick) const {
        return line_[port]->rx.CycleOfTick(tick);
    }

    void Save(uint32_t port, StateWriter& w);
    void Restore(uint32_t port, StateReader& r);

private:
    static constexpr uint32_t kPorts = 3u;

    struct Line {
        Line(Sa11xxUartBase* o, const char* name) : owner(o), rx(name) {}

        Sa11xxUartBase* owner;
        UartRxLine      rx;
    };

    static UartRxLine::Frame FrameOf(const Control& ctl);
    void StartClock(Line& l, const Control& ctl, uint64_t now);
    void RateOverflow(const Line& l);
    static void LogLoopback(const Line& l, size_t n);

    GuestCycleClock* clock_ = nullptr;
    std::array<std::unique_ptr<Line>, kPorts> line_{};
    uint32_t count_ = 0;
};

}
