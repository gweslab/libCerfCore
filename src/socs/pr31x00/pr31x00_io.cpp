#include "pr31x00_io.h"

#include "pr31x00_intc.h"
#include "pr31x00_io_pins.h"
#include "pr31x00_mfio_slave.h"
#include "pr31x00_reset_line.h"

#include <cerfcore/boards/board_context.h>
#include "pr31500_id.h"
#include "pr31700_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x10C00180u;

constexpr uint32_t kOffIoCtl     = 0x00u;   /* $180 */
constexpr uint32_t kOffMfioDout  = 0x04u;   /* $184 */
constexpr uint32_t kOffMfioDirec = 0x08u;   /* $188 */
constexpr uint32_t kOffMfioDin   = 0x0Cu;   /* $18C read-only */
constexpr uint32_t kOffMfioSel   = 0x10u;   /* $190 */
constexpr uint32_t kOffIoPd      = 0x14u;   /* $194 */
constexpr uint32_t kOffMfioPd    = 0x18u;   /* $198 */

/* I/O Control (§9.3.1): IODEBSEL[6:0]<30:24> IODIREC[6:0]<22:16> IODOUT[6:0]<14:8>
   IODIN[6:0]<6:0>(R); bits 31, 23, 15 and 7 reserved. */
constexpr uint32_t kIoCtlReserved  = 0x80808080u;
constexpr uint32_t kIoCtlWritable  = 0x7F7F7F00u;
constexpr uint32_t kIoDirecShift   = 16;
constexpr uint32_t kIoDoutShift    = 8;
constexpr uint32_t kIoPinMask      = 0x7Fu;   /* 7 general purpose I/O ports */

/* I/O Power-Down (§9.3.6): IOPD[6:0]<6:0>; bits 31-7 reserved. */
constexpr uint32_t kIoPdReserved = 0xFFFFFF80u;

}  /* namespace */

bool Pr31x00Io::ShouldRegister() {
    const std::string_view soc = emu_.Get<BoardContext>().GetSocId();
    return soc == SocId::Pr31500 || soc == SocId::Pr31700;
}

void Pr31x00Io::OnReady() {
    intc_ = &emu_.Get<Pr31x00Intc>();
    emu_.Get<PeripheralDispatcher>().Register(this);
    auto& reset_line = emu_.Get<Pr31x00ResetLine>();
    reset_line.RegisterResetListener([this] {
        regs_         = kResetRegs;
        powered_down_ = false;
    });
    reset_line.RegisterReleaseListener([this] {
        NotifyMfioOut();
        NotifyIoOut();
    });
    auto& sleep = emu_.Get<GuestDeepSleep>();
    sleep.RegisterSleepEntryListener([this] { SetPoweredDown(true); });
    sleep.RegisterPowerUpListener([this] { SetPoweredDown(false); });
}

uint32_t Pr31x00Io::MfioOutputs() const { return regs_.mfio_direc & regs_.mfio_sel; }
uint32_t Pr31x00Io::IoOutputs() const { return (regs_.io_ctl >> kIoDirecShift) & kIoPinMask; }
uint32_t Pr31x00Io::IoDout() const { return (regs_.io_ctl >> kIoDoutShift) & kIoPinMask; }

Pr31x00Pins Pr31x00Io::PinsOf(uint32_t dout, uint32_t outputs, uint32_t pd) const {
    const uint32_t pulled = powered_down_ ? pd : 0u;
    return {dout & ~pulled, outputs & ~pulled, pulled};
}

Pr31x00Pins Pr31x00Io::MfioPins() const {
    return PinsOf(regs_.mfio_dout, MfioOutputs(), regs_.mfio_pd);
}

Pr31x00Pins Pr31x00Io::IoPins() const {
    return PinsOf(IoDout(), IoOutputs(), regs_.io_pd & kIoPinMask);
}

void Pr31x00Io::SetPoweredDown(bool powered_down) {
    powered_down_ = powered_down;
    NotifyMfioOut();
    NotifyIoOut();
}

uint32_t Pr31x00Io::ReadWord(uint32_t addr) {
    switch (addr - kBase) {
        case kOffIoCtl: {
            /* IODIN reports the pin state whether the port drives it or not
               (§9.3.1): an output port reads back IODOUT, an input port reads
               the level the board drives onto the pin. */
            const uint32_t direc  = IoOutputs();
            const uint32_t inputs = kIoPinMask & ~direc;
            uint32_t din = IoDout() & direc;
            if (inputs) {
                auto* pins = emu_.TryGet<Pr31x00IoPins>();
                if (!pins) {
                    HaltUnsupportedAccess("PR31x00 IO IODIN on an input port with no board wiring",
                                          addr, inputs);
                }
                din |= (io_din_.load() | pins->IoDin()) & inputs;
            }
            return regs_.io_ctl | din;
        }

        case kOffMfioDin: {
            /* MFIODIN returns each pin's logic state regardless of direction
               (§9.3.4): a pin selected bi-directional and set to output
               (direc & sel) reads back MFIODOUT, every other pin the level
               driven onto it (the board, or DriveMfioInput). */
            const uint32_t outputs = MfioOutputs();
            const uint32_t inputs  = ~outputs;
            uint32_t din = regs_.mfio_dout & outputs;
            if (inputs) {
                auto* pins = emu_.TryGet<Pr31x00IoPins>();
                std::optional<uint32_t> board = pins ? pins->MfioDin() : std::nullopt;
                if (!board) {
                    HaltUnsupportedAccess("PR31x00 IO MFIODIN on an input pin with no board wiring",
                                          addr, inputs);
                }
                din |= (mfio_din_.load() | *board) & inputs;
            }
            return din;
        }

        case kOffMfioDout:  return regs_.mfio_dout;
        case kOffMfioDirec: return regs_.mfio_direc;
        case kOffMfioSel:   return regs_.mfio_sel;
        case kOffIoPd:      return regs_.io_pd;
        case kOffMfioPd:    return regs_.mfio_pd;
        default:            HaltUnsupportedAccess("PR31x00 IO ReadWord", addr, 0);
    }
}

void Pr31x00Io::WriteWord(uint32_t addr, uint32_t value) {
    switch (addr - kBase) {
        case kOffIoCtl:
            if (value & kIoCtlReserved) {
                HaltUnsupportedAccess("PR31x00 IO IO_CTL reserved", addr, value);
            }
            regs_.io_ctl = value & kIoCtlWritable;
            NotifyIoOut();
            return;

        case kOffMfioDout:  regs_.mfio_dout  = value; NotifyMfioOut(); return;
        case kOffMfioDirec: regs_.mfio_direc = value; NotifyMfioOut(); return;
        case kOffMfioSel:   regs_.mfio_sel   = value; NotifyMfioOut(); return;
        case kOffMfioPd:    regs_.mfio_pd    = value; return;

        case kOffIoPd:
            if (value & kIoPdReserved) {
                HaltUnsupportedAccess("PR31x00 IO IOPWRDN reserved", addr, value);
            }
            regs_.io_pd = value;
            return;

        case kOffMfioDin:   /* read-only (§9.3.4) */
        default:            HaltUnsupportedAccess("PR31x00 IO WriteWord", addr, value);
    }
}

/* MFIOPOSINT[n] latches a 0->1 transition of pin n and MFIONEGINT[n] a 1->0 one
   (§8.3.3, §8.3.4); they are Interrupt Status 3 and 4, so Clear/Status sets 2 and 3. */
void Pr31x00Io::DriveMfioInput(uint32_t pin, bool level) {
    const uint32_t bit = 1u << pin;
    const uint32_t old = level ? mfio_din_.fetch_or(bit) : mfio_din_.fetch_and(~bit);
    if (((old & bit) != 0u) == level) return;
    intc_->SetPending(level ? 2u : 3u, bit);
}

/* A rising edge on pin n latches IOPOSINT[n] (Interrupt Status 5 bit 7+n), a
   falling edge IONEGINT[n] (bit n); both are in Interrupt Status 5, so Clear/
   Status set 4 (§8.3.5). */
void Pr31x00Io::DriveIoInput(uint32_t pin, bool level) {
    const uint32_t bit = 1u << pin;
    const uint32_t old = level ? io_din_.fetch_or(bit) : io_din_.fetch_and(~bit);
    if (((old & bit) != 0u) == level) return;
    intc_->SetPending(4u, level ? (1u << (7u + pin)) : bit);
}

void Pr31x00Io::NotifyMfioOut() {
    const Pr31x00Pins p = MfioPins();
    mfio_out_observers_.Call(p);
    if (auto* slave = emu_.TryGet<Pr31x00MfioSlave>()) slave->OnMfioOut(p);
}

void Pr31x00Io::NotifyIoOut() {
    const Pr31x00Pins p = IoPins();
    io_out_observers_.Call(p);
}

void Pr31x00Io::RegisterMfioOutObserver(PinObserver cb) {
    cb(MfioPins());
    mfio_out_observers_.Add(std::move(cb));
}

void Pr31x00Io::RegisterIoOutObserver(PinObserver cb) {
    cb(IoPins());
    io_out_observers_.Add(std::move(cb));
}

void Pr31x00Io::SaveState(StateWriter& w) {
    w.Write("io_ctl", regs_.io_ctl);
    w.Write("mfio_dout", regs_.mfio_dout);
    w.Write("mfio_direc", regs_.mfio_direc);
    w.Write("mfio_sel", regs_.mfio_sel);
    w.Write("io_pd", regs_.io_pd);
    w.Write("mfio_pd", regs_.mfio_pd);
    w.Write("powered_down", powered_down_);
    w.Write("mfio_din", mfio_din_.load());
    w.Write("io_din", io_din_.load());
    if (auto* slave = emu_.TryGet<Pr31x00MfioSlave>()) slave->SaveState(w);
}

void Pr31x00Io::RestoreState(StateReader& r) {
    r.Read("io_ctl", regs_.io_ctl);
    r.Read("mfio_dout", regs_.mfio_dout);
    r.Read("mfio_direc", regs_.mfio_direc);
    r.Read("mfio_sel", regs_.mfio_sel);
    r.Read("io_pd", regs_.io_pd);
    r.Read("mfio_pd", regs_.mfio_pd);
    r.Read("powered_down", powered_down_);
    uint32_t din = 0;
    r.Read("mfio_din", din); mfio_din_.store(din);
    r.Read("io_din", din); io_din_.store(din);
    if (auto* slave = emu_.TryGet<Pr31x00MfioSlave>()) slave->RestoreState(r);
}

REGISTER_SERVICE(Pr31x00Io);

}
