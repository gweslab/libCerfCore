#include "pr31x00_uart_dma.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/host_request_channel.h>
#include <cerfcore/state/state_stream.h>
#include "pr31500_id.h"
#include "pr31700_id.h"

#include <utility>

namespace cerfcore {

namespace {

/* ADDR CNT, 16 bits (TMPR3911 Figure 16.2.1, printed p.16-6). */
constexpr uint32_t kAddrCntMask = 0x0000FFFFu;

}

bool Pr31x00UartDma::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    if (!bd) return false;
    const std::string_view soc = bd->GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00UartDma::OnReady() {
    mem_           = &emu_.Get<EmulatedMemory>();
    clock_         = &emu_.Get<GuestCycleClock>();
    host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    host_requests_->RegisterListener([this] { OnHostRequest(); });
    clock_->RegisterRateListener([this] {
        const uint64_t now = clock_->Cycles();
        for (uint32_t i = 0; i < count_; ++i) {
            Channel& c = *ch_[i];
            std::lock_guard<std::mutex> lk(c.mu);
            if (!c.line.Rescale(clock_->ClockRate(), now)) {
                emu_.Get<Fatal>().Die("PR31x00 UART %s RX: the core rate change overflows the bit "
                                      "counter scale", c.source);
            }
            ArmLocked(c, now);
        }
    });
}

uint32_t Pr31x00UartDma::Attach(const char* source, RxIntFn raise_ints, LineIdleFn on_line_idle) {
    if (count_ >= kPorts) emu_.Get<Fatal>().Die("Pr31x00UartDma: a third UART attached");
    const uint32_t port = count_;
    ch_[port] = std::make_unique<Channel>(source);
    ch_[port]->raise_ints   = std::move(raise_ints);
    ch_[port]->on_line_idle = std::move(on_line_idle);
    ch_[port]->event        = clock_->Add([this, port] { OnCharEvent(*ch_[port]); });
    ++count_;
    return port;
}

Pr31x00UartDma::Channel& Pr31x00UartDma::Ch(uint32_t port) { return *ch_[port]; }
const Pr31x00UartDma::Channel& Pr31x00UartDma::Ch(uint32_t port) const { return *ch_[port]; }

void Pr31x00UartDma::SetBuffer(uint32_t port, uint32_t pa) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    c.buffer_pa = pa;
}

void Pr31x00UartDma::SetLength(uint32_t port, uint32_t bytes) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    c.length = bytes;
}

/* TMPR3911 §16.2.4 (page 16-5): "For the receiver, this means that when the Receive Holding
   Register becomes full, the DMA controller will transfer a byte from the Receive Holding
   Register to the specified buffer". */
void Pr31x00UartDma::SetControl(uint32_t port, bool receiver_on, bool armed) {
    Channel& c = Ch(port);
    RxInts   ints;
    bool     idle;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        const uint64_t now = clock_->Cycles();
        idle = SettleLocked(c, now, ints);
        c.receiver_on = receiver_on;
        /* TMPR3911 §16.5.1 (page 16-13): with ENUART cleared "the module is kept in a reset
           state"; sharp_mobilon_hc4100_hpc2 serial.dll sub_1474498 reopens with ENDMARX still set
           and its read cursor at the buffer base, and sub_1474634 reads to base + DMACNT. */
        if (!receiver_on) {
            idle = idle || !IdleLocked(c);
            c.line.Reset();
            clock_->Disarm(c.event);
            c.count           = 0;
            c.rxhold_full     = false;
            c.prxhold_full    = false;
            c.prxhold_overrun = false;
        }
        if (armed != c.armed) {
            c.armed = armed;
            if (armed) {
                c.count = 0;
                DrainHoldsLocked(c, ints);
            }
        }
        if (receiver_on) ArmLocked(c, now);
    }
    Notify(c, ints, idle);
}

/* TMPR3911 §16.5.1 (page 16-11): PRXHOLDFULL and RXHOLDFULL RESET 0. */
void Pr31x00UartDma::ResetLine(uint32_t port) {
    Channel& c = Ch(port);
    bool     dropped;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        c.receiver_on     = false;
        c.armed           = false;
        c.rxhold_full     = false;
        c.prxhold_full    = false;
        c.prxhold_overrun = false;
        dropped           = !IdleLocked(c);
        c.line.Reset();
        clock_->Disarm(c.event);
    }
    Notify(c, RxInts{}, dropped);
}

void Pr31x00UartDma::SetLine(uint32_t port, const LineTiming& timing) {
    Channel& c = Ch(port);
    RxInts   ints;
    bool     idle;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        const uint64_t   now    = clock_->Cycles();
        const bool       landed = SettleLocked(c, now, ints);
        const LineTiming old    = c.timing;
        c.timing = timing;
        idle     = RetimeLocked(c, old, now) || landed;
    }
    Notify(c, ints, idle);
}

/* TMPR3911 §16.4 (page 16-9): the next 7 or 8 bits "(depending on BIT_7 in the Control 1 Register)";
   Figure 16.4.1: BAUDRATE[9:0] and the UART master clock feed the bit counter. §16.2.1 (page 16-2):
   "each UART must also have its clock enabled to operate". */
bool Pr31x00UartDma::RetimeLocked(Channel& c, const LineTiming& old, uint64_t now) {
    if (!c.line.Timed()) return false;
    if (c.timing.uart_clock.num == 0u) {
        const bool dropped = c.line.DropNoClock();
        clock_->Disarm(c.event);
        return dropped && IdleLocked(c);
    }
    c.line.Reframe(FrameOf(old), FrameOf(c.timing));
    bool rerated = false;
    if (!c.line.SetBitRate(clock_->ClockRate(), BitRateLocked(c), now, rerated)) {
        emu_.Get<Fatal>().Die("PR31x00 UART %s RX: the bit rate change overflows the bit counter "
                              "scale", c.source);
    }
    ArmLocked(c, now);
    return false;
}

UartRxLine::Frame Pr31x00UartDma::FrameOf(const LineTiming& t) {
    return UartRxLine::Frame{2u * uint64_t{t.transfer_bits}, 2u * uint64_t{t.frame_bits}};
}

GuestCycleClock::Rate Pr31x00UartDma::BitRateLocked(const Channel& c) {
    const GuestCycleClock::Rate uart = c.timing.uart_clock;
    if (!uart.ScaleFits(1u, c.timing.uart_clocks_per_bit)) {
        emu_.Get<Fatal>().Die("PR31x00 UART %s RX: the %llu/%llu Hz UART clock over %llu clocks "
                              "per bit overflows the bit rate", c.source,
                              static_cast<unsigned long long>(uart.num),
                              static_cast<unsigned long long>(uart.den),
                              static_cast<unsigned long long>(c.timing.uart_clocks_per_bit));
    }
    return uart.Scaled(1u, c.timing.uart_clocks_per_bit);
}

bool Pr31x00UartDma::IdleLocked(const Channel& c) const {
    return c.line.Pending() == 0u && c.line.WireEmpty();
}

uint32_t Pr31x00UartDma::Count(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.count;
}

uint32_t Pr31x00UartDma::Length(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return c.length;
}

bool Pr31x00UartDma::LineIdle(uint32_t port) const {
    const Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    return IdleLocked(c);
}

void Pr31x00UartDma::ReadHolds(uint32_t port, bool& rxhold_full, bool& prxhold_full) {
    Channel& c = Ch(port);
    RxInts   ints;
    bool     idle;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        const uint64_t now     = clock_->Cycles();
        const size_t   pending = c.line.Pending();
        idle = SettleLocked(c, now, ints);
        if (c.line.Pending() != pending) ArmLocked(c, now);
        rxhold_full  = c.rxhold_full;
        prxhold_full = c.prxhold_full;
    }
    Notify(c, ints, idle);
}

void Pr31x00UartDma::Receive(uint32_t port, const uint8_t* data, size_t n) {
    Channel& c = Ch(port);
    {
        std::lock_guard<std::mutex> lk(c.mu);
        if (!c.receiver_on || !c.line.Push(data, n)) return;
    }
    host_requests_->Request();
}

/* TMPR3911 §16.2.4 (pages 16-5 / 16-6): "For the transmitter, this means that when the
   Transmit Holding Register becomes empty, the DMA controller will transfer a byte from the
   specified buffer"; with ENDMALOOP clear "DMA will stop when the end of the buffer is reached". */
void Pr31x00UartDma::TransmitBlock(uint32_t port, std::vector<uint8_t>& out) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    out.resize(c.length);
    for (uint32_t i = 0; i < c.length; ++i) out[i] = mem_->ReadByte(c.buffer_pa + i);
    c.count = c.length - 1u;
}

void Pr31x00UartDma::OnHostRequest() {
    const uint64_t now = clock_->Cycles();
    for (uint32_t i = 0; i < count_; ++i) {
        Channel& c    = *ch_[i];
        bool     idle = false;
        {
            std::lock_guard<std::mutex> lk(c.mu);
            const std::vector<uint8_t> bytes = c.line.TakeWire();
            if (!bytes.empty()) {
                if (c.timing.uart_clock.num == 0u) {
                    c.line.LogNoClock(bytes.size());
                    idle = IdleLocked(c);
                } else {
                    if (!c.line.Receive(bytes.data(), bytes.size(), clock_->ClockRate(),
                                        BitRateLocked(c), FrameOf(c.timing), now)) {
                        emu_.Get<Fatal>().Die("PR31x00 UART %s RX: the bit rate overflows the bit "
                                              "counter scale", c.source);
                    }
                    ArmLocked(c, now);
                }
            }
        }
        Notify(c, RxInts{}, idle);
    }
}

void Pr31x00UartDma::ArmLocked(Channel& c, uint64_t now) {
    if (c.line.Pending() == 0u || (!c.armed && c.rxhold_full)) {
        clock_->Disarm(c.event);
        return;
    }
    clock_->Arm(c.event, c.line.FrontLandCycle(now));
}

/* TMPR3911 §16.4 (page 16-9): "As the stop bit is clocked in and checked, the assembled character
   is transferred from the Receive Shift Register to the PRXHOLD Register". */
bool Pr31x00UartDma::SettleLocked(Channel& c, uint64_t now, RxInts& ints) {
    bool landed = false;
    while (c.line.FrontDue(now)) {
        ReceiveByteLocked(c, static_cast<uint8_t>(c.line.Front()), ints);
        c.line.PopFront(now);
        landed = true;
    }
    return landed && IdleLocked(c);
}

void Pr31x00UartDma::OnCharEvent(Channel& c) {
    RxInts ints;
    bool   idle;
    {
        std::lock_guard<std::mutex> lk(c.mu);
        const uint64_t now = clock_->Cycles();
        idle = SettleLocked(c, now, ints);
        ArmLocked(c, now);
    }
    Notify(c, ints, idle);
}

void Pr31x00UartDma::ReceiveByteLocked(Channel& c, uint8_t byte, RxInts& ints) {
    if (c.armed) {
        StoreLocked(c, byte, ints);
    } else if (!c.rxhold_full) {
        c.rxhold      = byte;
        c.rxhold_full = true;
        ints.rx       = true;
    } else if (!c.prxhold_full) {
        c.prxhold      = byte;
        c.prxhold_full = true;
    } else {
        /* TMPR3911 §16.4.3 (page 16-10): "The new character will overwrite the character in the
           PRXHOLD Register"; the interrupt "is not asserted until the character associated with
           the overrun error is transferred to the Receive Holding Register". */
        c.prxhold         = byte;
        c.prxhold_overrun = true;
    }
}

/* TMPR3911 Figure 16.2.1 (printed p.16-6): ADDR CNT against DMA LENGTH, the buffer length minus
   one (§16.2.4), gives the end-of-buffer interrupt, and against DMA LENGTH / 2 the half-buffer
   interrupt. */
void Pr31x00UartDma::StoreLocked(Channel& c, uint8_t byte, RxInts& ints) {
    if (c.length == 0u) {
        emu_.Get<Fatal>().Die("PR31x00 UART %s RX DMA armed with no DMALENGTH programmed",
                              c.source);
    }
    const uint32_t at  = c.count;
    const uint32_t end = c.length - 1u;
    mem_->WriteByte(c.buffer_pa + at, byte);
    if (at == end / 2u) ints.dma_half = true;
    if (at == end) {
        ints.dma_full = true;
        c.count       = 0;
    } else {
        c.count = (at + 1u) & kAddrCntMask;
    }
    ints.rx = true;
}

void Pr31x00UartDma::DrainHoldsLocked(Channel& c, RxInts& ints) {
    if (c.rxhold_full) {
        StoreLocked(c, c.rxhold, ints);
        c.rxhold_full = false;
    }
    if (c.prxhold_full) {
        if (c.prxhold_overrun) ints.overrun = true;
        StoreLocked(c, c.prxhold, ints);
        c.prxhold_full    = false;
        c.prxhold_overrun = false;
    }
}

void Pr31x00UartDma::Notify(Channel& c, const RxInts& ints, bool line_idle) {
    if (ints.rx || ints.overrun || ints.dma_half || ints.dma_full) c.raise_ints(ints);
    if (line_idle) c.on_line_idle();
}

void Pr31x00UartDma::SaveState(uint32_t port, StateWriter& w) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    w.Write("buffer_pa", c.buffer_pa);
    w.Write("length", c.length);
    w.Write("count", c.count);
    w.Write<uint8_t>("armed", c.armed ? 1u : 0u);
    w.Write<uint8_t>("receiver_on", c.receiver_on ? 1u : 0u);
    w.Write("rxhold", c.rxhold);
    w.Write<uint8_t>("rxhold_full", c.rxhold_full ? 1u : 0u);
    w.Write("prxhold", c.prxhold);
    w.Write<uint8_t>("prxhold_full", c.prxhold_full ? 1u : 0u);
    w.Write<uint8_t>("prxhold_overrun", c.prxhold_overrun ? 1u : 0u);
    c.line.Save(w, clock_->Cycles());
}

void Pr31x00UartDma::RestoreState(uint32_t port, StateReader& r, const LineTiming& framing) {
    Channel& c = Ch(port);
    std::lock_guard<std::mutex> lk(c.mu);
    r.Read("buffer_pa", c.buffer_pa);
    r.Read("length", c.length);
    r.Read("count", c.count);
    uint8_t flag = 0;
    r.Read("armed", flag);
    c.armed = flag != 0u;
    r.Read("receiver_on", flag);
    c.receiver_on = flag != 0u;
    r.Read("rxhold", c.rxhold);
    r.Read("rxhold_full", flag);
    c.rxhold_full = flag != 0u;
    r.Read("prxhold", c.prxhold);
    r.Read("prxhold_full", flag);
    c.prxhold_full = flag != 0u;
    r.Read("prxhold_overrun", flag);
    c.prxhold_overrun = flag != 0u;
    c.line.Restore(r, clock_->ClockRate(), clock_->Cycles());
    c.timing = framing;
    clock_->Disarm(c.event);
}

REGISTER_SERVICE(Pr31x00UartDma);

}
