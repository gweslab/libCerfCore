#include "rcvdl56dpfl_data_pump_peripheral.h"

#include "rcvdl56dpfl_bit_tick.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>
#include <cerfengine/host_request_channel.h>
#include "../../execution/engine_bridge.h"

namespace cerfcore {

void Rcvdl56dpflDataPumpPeripheral::StartDataPump(const Rcvdl56dpflSelfTest& self_test) {
    pump_.SetSelfTestResults(self_test);
    clock_ = &emu_.Get<GuestCycleClock>();
    event_ = clock_->Add([this] { pump_.TimerElapsed(); });
    clock_->RegisterRateListener([this] { RescaleBitClock(); });
    requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    requests_->RegisterListener([this] { pump_.ServiceHostData(); });
    emu_.Get<Hibernation>().RegisterPreSaveListener([this] { pump_.ServiceHostData(); });
    phone_.SetWake([this] { requests_->Request(); });
    phone_.RegisterWidget();
}

void Rcvdl56dpflDataPumpPeripheral::OnShutdown() {
    phone_.Shutdown();
}

void Rcvdl56dpflDataPumpPeripheral::Halt(const char* reason) {
    emu_.Get<Fatal>().Die("[MDP] %s", reason);
}

uint8_t Rcvdl56dpflDataPumpPeripheral::ReadA1AddressedRegister(uint32_t addr) {
    return pump_.Read(A1AddressedRegister(addr, 0u));
}

void Rcvdl56dpflDataPumpPeripheral::WriteA1AddressedRegister(uint32_t addr, uint8_t value) {
    const uint32_t reg = A1AddressedRegister(addr, value);
    if (const char* refused = pump_.Write(reg, value))
        emu_.Get<Fatal>().Die("[MDP] register %02Xh write 0x%02X: %s", reg, value, refused);
}

uint32_t Rcvdl56dpflDataPumpPeripheral::A1AddressedRegister(uint32_t addr, uint8_t value) {
    const uint32_t offset = addr - MmioBase();
    if (offset & 1u) HaltUnsupportedAccess("odd-address byte access", addr, value);
    return offset >> 1;
}

void Rcvdl56dpflDataPumpPeripheral::SetResetPin(bool high) {
    pump_.SetResetPin(high);
}

void Rcvdl56dpflDataPumpPeripheral::SaveState(StateWriter& w) {
    pump_.SaveState(w);
    const TickPosition pos = timed_ ? bit_ticks_.PositionAt(clock_->Cycles()) : TickPosition{};
    w.Write<uint8_t>("bit_clock_timed", timed_ ? 1u : 0u);
    w.Write<uint64_t>("bit_clock_ticks", pos.ticks);
    w.Write<uint64_t>("bit_clock_phase", pos.phase);
    w.Write<uint64_t>("bit_clock_phase_den", pos.phase_den);
    w.Write<uint64_t>("bit_rate_num", bit_rate_.num);
    w.Write<uint64_t>("bit_rate_den", bit_rate_.den);
}

void Rcvdl56dpflDataPumpPeripheral::RestoreState(StateReader& r) {
    pump_.RestoreState(r);
    uint8_t      timed = 0u;
    TickPosition pos;
    r.Read("bit_clock_timed", timed);
    r.Read("bit_clock_ticks", pos.ticks);
    r.Read("bit_clock_phase", pos.phase);
    r.Read("bit_clock_phase_den", pos.phase_den);
    r.Read("bit_rate_num", bit_rate_.num);
    r.Read("bit_rate_den", bit_rate_.den);
    timed_ = timed != 0u;
    if (timed_ && (!bit_ticks_.SetRate(clock_->ClockRate(), bit_rate_) ||
                   !bit_ticks_.PlaceAt(clock_->Cycles(), pos)))
        r.Reject("[MDP] bit clock %llu/%llu Hz does not fit the core clock",
                 static_cast<unsigned long long>(bit_rate_.num), static_cast<unsigned long long>(bit_rate_.den));
    pump_.RearmTimer();
}

void Rcvdl56dpflDataPumpPeripheral::SetIrqLevel(bool asserted) {
    DriveIrq(asserted);
}

void Rcvdl56dpflDataPumpPeripheral::SetBitRate(uint32_t bits_per_second) {
    const GuestCycleClock::Rate bit{bits_per_second, 1u};
    const uint64_t              now = clock_->Cycles();
    if (!timed_) {
        if (!bit_ticks_.SetRate(clock_->ClockRate(), bit))
            emu_.Get<Fatal>().Die("[MDP] bit rate %u does not fit the core clock", bits_per_second);
        bit_ticks_.Start(now);
        timed_    = true;
        bit_rate_ = bit;
        return;
    }
    if (bit == bit_rate_) return;
    if (!bit_ticks_.Rescale(now, clock_->ClockRate(), bit))
        emu_.Get<Fatal>().Die("[MDP] bit rate %u does not fit the core clock", bits_per_second);
    bit_rate_ = bit;
}

uint64_t Rcvdl56dpflDataPumpPeripheral::BitTickNow() {
    if (!timed_) return 0u;
    const TickPosition pos = bit_ticks_.PositionAt(clock_->Cycles());
    return pos.ticks + (pos.phase != 0u ? 1u : 0u);
}

uint64_t Rcvdl56dpflDataPumpPeripheral::BitTicksElapsed() {
    if (!timed_) return 0u;
    return bit_ticks_.PositionAt(clock_->Cycles()).ticks;
}

void Rcvdl56dpflDataPumpPeripheral::ArmTimer(uint64_t tick) {
    if (tick == kRcvdl56dpflNoEvent) {
        clock_->Disarm(event_);
        return;
    }
    clock_->Arm(event_, bit_ticks_.CycleOfTick(tick));
}

int64_t Rcvdl56dpflDataPumpPeripheral::GuestNs() {
    return clock_->NowNs();
}

void Rcvdl56dpflDataPumpPeripheral::RescaleBitClock() {
    if (!timed_) return;
    if (!bit_ticks_.Rescale(clock_->Cycles(), clock_->ClockRate(), bit_rate_))
        emu_.Get<Fatal>().Die("[MDP] the core rate change overflows the bit clock");
    pump_.RearmTimer();
}

}
