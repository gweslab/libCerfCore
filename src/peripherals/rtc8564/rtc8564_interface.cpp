#include "rtc8564_core.h"
#include "rtc8564_wiring.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/cycle_anchored_counter.h"
#include "../../socs/i2c_register_pointer.h"
#include "../../socs/iop13xx/iop13xx_i2c_device.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

namespace {

constexpr uint8_t  kSlaveAddress = 0xA2u >> 1;
constexpr uint8_t  kPointerMask  = 0x0Fu;
constexpr uint32_t kPhaseIdle    = 0u;
constexpr uint32_t kPhaseSelect  = 1u;
constexpr uint32_t kPhaseWrite   = 2u;
constexpr uint32_t kPhaseRead    = 3u;

class Rtc8564Interface final : public Iop13xxI2cDevice {
public:
    using Iop13xxI2cDevice::Iop13xxI2cDevice;

    bool ShouldRegister() override { return emu_.TryGet<Rtc8564Wiring>() != nullptr; }

    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        core_  = &emu_.Get<Rtc8564Core>();
        clock_->RegisterRateListener([this] { OnRateChange(); });
    }

    void BusStart() override {
        std::lock_guard<std::mutex> guard(mutex_);
        const uint64_t now = clock_->Cycles();
        if (!open_ || TimedOut(now)) Open(now);
        pointer_.EndSelect();
        writable_  = false;
        read_mode_ = false;
    }

    bool MatchesAddress(uint8_t slave_addr) const override { return slave_addr == kSlaveAddress; }

    void TxnStart(uint8_t) override {
        std::lock_guard<std::mutex> guard(mutex_);
        pointer_.Start();
        writable_  = false;
        read_mode_ = false;
        core_->Update();
    }

    /* NXP UM10204 section 3.1.6 (p. 10): SDA left HIGH in the acknowledge clock
       pulse is the Not Acknowledge. */
    bool TxnWriteByte(uint8_t, uint8_t value) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (TimedOut(clock_->Cycles())) return false;
        if (pointer_.Select(value & kPointerMask)) {
            writable_ = true;
            return true;
        }
        if (!writable_ || read_mode_) return false;
        core_->WriteAt(Pointer(), value);
        if (core_->StopSet()) stop_seen_ = true;
        IncrementPointer();
        return true;
    }

    uint8_t TxnReadByte(uint8_t) override {
        std::lock_guard<std::mutex> guard(mutex_);
        if (TimedOut(clock_->Cycles())) return 0xFFu;
        pointer_.EndSelect();
        writable_  = false;
        read_mode_ = true;
        const uint8_t value = core_->ReadAt(Pointer());
        IncrementPointer();
        return value;
    }

    void BusStop() override {
        std::lock_guard<std::mutex> guard(mutex_);
        pointer_.EndSelect();
        writable_  = false;
        read_mode_ = false;
        open_      = false;
        timed_out_ = false;
    }

    void SaveState(StateWriter& writer) override {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            const uint64_t now = clock_->Cycles();
            writer.Write("pointer", Pointer());
            writer.Write("phase", Phase());
            writer.Write("read_mode", read_mode_);
            writer.Write("bus_open", open_);
            writer.Write("bus_timed_out", timed_out_);
            writer.Write("bus_stop_seen", stop_seen_);
            writer.Write<uint32_t>("bus_seconds", open_ ? window_.CountAt(now) : 0u);
            writer.Write<uint64_t>("bus_phase", open_ ? window_.PhaseAt(now) : 0u);
            writer.Write<uint64_t>("bus_phase_den", window_.PhaseDenominator());
        }
        core_->SaveState(writer);
    }

    void RestoreState(StateReader& reader) override {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            uint8_t  pointer   = 0;
            uint32_t txn_phase = kPhaseIdle;
            uint32_t seconds   = 0;
            uint64_t phase = 0, den = 1;
            reader.Read("pointer", pointer);
            reader.Read("phase", txn_phase);
            reader.Read("read_mode", read_mode_);
            pointer_.Restore(pointer, true, txn_phase == kPhaseSelect);
            writable_ = txn_phase == kPhaseWrite;
            reader.Read("bus_open", open_);
            reader.Read("bus_timed_out", timed_out_);
            reader.Read("bus_stop_seen", stop_seen_);
            reader.Read("bus_seconds", seconds);
            reader.Read("bus_phase", phase);
            reader.Read("bus_phase_den", den);
            SetWindowRatio();
            window_.AnchorAtPhase(clock_->Cycles(), seconds, phase, den);
        }
        core_->RestoreState(reader);
    }

    void PostRestore() override { core_->PostRestore(); }

private:
    uint8_t Pointer() const { return static_cast<uint8_t>(pointer_.Register()); }

    uint32_t Phase() const {
        if (pointer_.Pending()) return kPhaseSelect;
        if (read_mode_) return kPhaseRead;
        return writable_ ? kPhaseWrite : kPhaseIdle;
    }

    void IncrementPointer() { pointer_.Point(static_cast<uint8_t>((Pointer() + 1u) & kPointerMask)); }

    void SetWindowRatio() {
        if (!window_.SetRatio(clock_->CpuHz(), 1u)) RatioOverflow();
    }

    [[noreturn]] void RatioOverflow() const {
        emu_.Get<Fatal>().Die("RTC8564: the %llu Hz CPU clock overflows the bus timeout ratio",
                              static_cast<unsigned long long>(clock_->CpuHz()));
    }

    void Open(uint64_t now) {
        open_      = true;
        timed_out_ = false;
        stop_seen_ = core_->StopSet();
        SetWindowRatio();
        window_.Anchor(now, 0u);
    }

    /* Epson RTC-8564 ETM11J-07 section 13.6.3 note 3) (p. 33): 1 s from START to
       STOP releases the interface to standby, writes are invalid and reads return
       all 1s until a new START; section 13.1.1 (p. 12): STOP = 1 disables it. */
    bool TimedOut(uint64_t now) {
        if (!open_ || timed_out_ || window_.CountAt(now) == 0u || core_->StopSet()) {
            return timed_out_;
        }
        if (stop_seen_) {
            emu_.Get<Fatal>().Die("RTC8564: I2C transfer %u s after START with STOP set and "
                                  "cleared since that START; whether STOP restarts the bus "
                                  "timeout is not modelled", window_.CountAt(now));
        }
        timed_out_ = true;
        return true;
    }

    void OnRateChange() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (open_ && !window_.Rescale(clock_->Cycles(), clock_->CpuHz(), 1u)) RatioOverflow();
    }

    GuestCycleClock*           clock_ = nullptr;
    Rtc8564Core*               core_  = nullptr;
    NarrowCycleAnchoredCounter window_;
    I2cRegisterPointer         pointer_;
    bool                       writable_  = false;
    bool                       read_mode_ = false;
    bool                       open_      = false;
    bool                       timed_out_ = false;
    bool                       stop_seen_ = false;
    std::mutex                 mutex_;
};

REGISTER_SERVICE_AS(Rtc8564Interface, Iop13xxI2cDevice);

} // namespace

}
