#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "../i2c_slave.h"
#include "../irq_controller.h"
#include "msm8255_clock_rates.h"
#include "msm8255_clock_reset.h"
#include "msm8255_i2c_slaves.h"

#include <cstdint>
#include <typeinfo>

namespace cerfcore {

namespace cerf_msm8255_i2c_detail {

inline constexpr uint32_t kWindowSize = 0x1000u;

inline constexpr uint32_t kRegWriteData       = 0x00u;
inline constexpr uint32_t kRegClkCtl          = 0x04u;
inline constexpr uint32_t kRegStatus          = 0x08u;
inline constexpr uint32_t kRegReadData        = 0x0Cu;
inline constexpr uint32_t kRegInterfaceSelect = 0x10u;

inline constexpr uint32_t kWriteDataByte = 0x0FFu;
inline constexpr uint32_t kWriteDataAddr = 1u << 8;
inline constexpr uint32_t kWriteDataLast = 1u << 9;
inline constexpr uint32_t kWriteDataBits = kWriteDataByte | kWriteDataAddr | kWriteDataLast;
inline constexpr uint32_t kAddrReadBit   = 1u;

inline constexpr uint32_t kClkCtlFsDivider = 0x0FFu;
inline constexpr uint32_t kClkCtlBits      = 0x7FFu;

inline constexpr uint32_t kStatusRdBufferFull = 1u << 1;
inline constexpr uint32_t kStatusPacketNacked = 1u << 3;
inline constexpr uint32_t kStatusBusActive    = 1u << 8;
inline constexpr uint32_t kStatusBusMaster    = 1u << 9;
inline constexpr uint32_t kStatusStateShift   = 10u;
inline constexpr uint32_t kStateTransmit      = 2u;
inline constexpr uint32_t kStateReceive       = 6u;

inline constexpr uint32_t kIntfSelect = 1u << 0;
inline constexpr uint32_t kIntfScl    = 1u << 8;
inline constexpr uint32_t kIntfSda    = 1u << 9;
inline constexpr uint32_t kIntfLines  = kIntfScl | kIntfSda;

inline constexpr uint32_t kNoSlave = Msm8255I2cSlaves::kNoSlave;

inline constexpr uint32_t kFsDividerBias  = 3u;
inline constexpr uint64_t kClocksPerByte  = 9u;
inline constexpr uint64_t kNsPerKhzPeriod = 1000000u;

enum class Phase : uint32_t { kIdle = 0u, kTransmit = 1u, kReceive = 2u };

template <uint32_t kBase, int kVicLine, uint32_t kCoreClock, uint32_t kBus>
class Msm8255I2cController : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        rx_event_    = emu_.Get<GuestCycleClock>().Add([this] { CompleteReception(); });
        clock_reset_ = &emu_.Get<Msm8255ClockReset>();
        clock_reset_->RegisterListener(kCoreClock, [this] { ResetController(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { ResetController(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kWindowSize; }

    uint32_t ReadWord(uint32_t addr) override {
        clock_reset_->RequireReleased(kCoreClock, addr);
        switch (addr - kBase) {
        case kRegClkCtl:          return clk_ctl_;
        case kRegStatus:          return Status();
        case kRegReadData:        return ReadData();
        case kRegInterfaceSelect: return intf_select_ | kIntfLines;
        }
        HaltUnsupportedAccess("ReadWord", addr, 0u);
    }

    uint8_t ReadByte(uint32_t addr) override {
        clock_reset_->RequireReleased(kCoreClock, addr);
        if (addr - kBase == kRegReadData) return static_cast<uint8_t>(ReadData());
        HaltUnsupportedAccess("ReadByte", addr, 0u);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        clock_reset_->RequireReleased(kCoreClock, addr);
        switch (addr - kBase) {
        case kRegWriteData:
            WriteData(value);
            return;
        case kRegClkCtl:
            if ((value & ~kClkCtlBits) != 0u) break;
            clk_ctl_ = value;
            return;
        case kRegInterfaceSelect:
            if ((value & ~(kIntfSelect | kIntfLines)) != 0u) break;
            intf_select_ = value & kIntfSelect;
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("i2c_clk_ctl", clk_ctl_);
        w.Write<uint32_t>("i2c_intf_select", intf_select_);
        w.Write<uint32_t>("i2c_phase", static_cast<uint32_t>(phase_));
        w.Write<uint32_t>("i2c_addr", addr_);
        w.Write<uint32_t>("i2c_rx_byte", rx_byte_);
        w.Write<uint8_t>("i2c_rx_held", rx_held_ ? 1u : 0u);
        w.Write<uint8_t>("i2c_rx_final", rx_final_ ? 1u : 0u);
        w.Write<uint8_t>("i2c_rx_busy", rx_busy_ ? 1u : 0u);
        w.Write<uint8_t>("i2c_last_pending", last_pending_ ? 1u : 0u);
        w.Write<uint64_t>("i2c_rx_due", rx_due_);
        w.Write<uint8_t>("i2c_nacked", nacked_ ? 1u : 0u);
        w.Write<uint32_t>("i2c_slave_index", slave_index_);
        emu_.Get<Msm8255I2cSlaves>().SaveBusState(kBus, w);
    }

    void RestoreState(StateReader& r) override {
        uint32_t phase = 0u;
        uint8_t  held = 0u, final_byte = 0u, busy = 0u, last = 0u;
        r.Read("i2c_clk_ctl", clk_ctl_);
        r.Read("i2c_intf_select", intf_select_);
        r.Read("i2c_phase", phase);
        r.Read("i2c_addr", addr_);
        r.Read("i2c_rx_byte", rx_byte_);
        r.Read("i2c_rx_held", held);
        r.Read("i2c_rx_final", final_byte);
        r.Read("i2c_rx_busy", busy);
        r.Read("i2c_last_pending", last);
        r.Read("i2c_rx_due", rx_due_);
        uint8_t nacked = 0u;
        r.Read("i2c_nacked", nacked);
        r.Read("i2c_slave_index", slave_index_);
        emu_.Get<Msm8255I2cSlaves>().RestoreBusState(kBus, r);
        if (phase > static_cast<uint32_t>(Phase::kReceive)) {
            r.Reject("Peripheral '%s': restored bus phase %u is not a phase this controller has",
                     typeid(*this).name(), phase);
        }
        phase_        = static_cast<Phase>(phase);
        rx_held_      = held != 0u;
        rx_final_     = final_byte != 0u;
        rx_busy_      = busy != 0u;
        last_pending_ = last != 0u;
        nacked_       = nacked != 0u;
    }

    void PostRestore() override {
        auto& clock = emu_.Get<GuestCycleClock>();
        clock.Disarm(rx_event_);
        if (rx_busy_) clock.Arm(rx_event_, rx_due_);
        emu_.Get<Msm8255I2cSlaves>().PostRestoreBus(kBus);
    }

private:
    uint32_t Status() const {
        uint32_t status = rx_held_ ? kStatusRdBufferFull : 0u;
        if (nacked_) status |= kStatusPacketNacked;
        if (phase_ == Phase::kTransmit) {
            status |= kStatusBusActive | kStatusBusMaster | (kStateTransmit << kStatusStateShift);
        } else if (phase_ == Phase::kReceive) {
            status |= kStatusBusActive | kStatusBusMaster | (kStateReceive << kStatusStateShift);
        }
        return status;
    }

    void WriteData(uint32_t value) {
        if ((value & ~kWriteDataBits) != 0u) {
            HaltUnsupportedAccess("WriteWord", kBase + kRegWriteData, value);
        }
        if ((value & kWriteDataAddr) != 0u) {
            StartAddress(value);
            return;
        }
        if (phase_ == Phase::kReceive) {
            if (value != kWriteDataLast) {
                emu_.Get<Fatal>().Die(
                    "Peripheral '%s': the guest wrote 0x%03X to WRITE_DATA while the bus "
                    "receives, and only a bare LAST_BYTE is modeled there",
                    typeid(*this).name(), value);
            }
            last_pending_ = true;
            return;
        }
        if (phase_ != Phase::kTransmit) {
            emu_.Get<Fatal>().Die(
                "Peripheral '%s': the guest wrote data byte 0x%03X to WRITE_DATA with no "
                "transfer started", typeid(*this).name(), value);
        }
        if (!Slave().TxnWriteByte(static_cast<uint8_t>(addr_),
                                  static_cast<uint8_t>(value & kWriteDataByte))) {
            emu_.Get<Fatal>().Die(
                "Peripheral '%s': slave 0x%02X refused data byte 0x%02X, and a NACKed "
                "data byte is not modeled", typeid(*this).name(), addr_,
                value & kWriteDataByte);
        }
        emu_.Get<IrqController>().PulseIrq(kVicLine);
        if ((value & kWriteDataLast) != 0u) Stop();
    }

    void StartAddress(uint32_t value) {
        if (rx_busy_ || rx_held_) {
            emu_.Get<Fatal>().Die(
                "Peripheral '%s': the guest started a transfer to 0x%02X while a received "
                "byte is still in the controller", typeid(*this).name(),
                (value >> 1) & 0x7Fu);
        }
        if (intf_select_ != 0u) {
            emu_.Get<Fatal>().Die(
                "Peripheral '%s': the guest addressed slave 0x%02X on the auxiliary "
                "interface, which no modeled board wires", typeid(*this).name(),
                (value >> 1) & 0x7Fu);
        }
        const bool read = (value & kAddrReadBit) != 0u;
        const bool last = (value & kWriteDataLast) != 0u;
        if (read && last) {
            emu_.Get<Fatal>().Die(
                "Peripheral '%s': the guest addressed slave 0x%02X for a zero-length read",
                typeid(*this).name(), (value >> 1) & 0x7Fu);
        }
        const bool     restart = phase_ != Phase::kIdle;
        const uint32_t addr    = (value >> 1) & 0x7Fu;
        auto&          slaves  = emu_.Get<Msm8255I2cSlaves>();
        const uint32_t index   = slaves.SlaveIndex(kBus, static_cast<uint8_t>(addr));
        if (index == kNoSlave) {
            if (!slaves.OwnedBySlave(kBus, static_cast<uint8_t>(addr))) {
                emu_.Get<Fatal>().Die("Peripheral '%s': no modeled slave answers address 0x%02X on "
                                      "bus %u", typeid(*this).name(), addr, kBus);
            }
            if (restart) Slave().BusStop();
            addr_        = addr;
            slave_index_ = kNoSlave;
            nacked_      = true;
            phase_       = Phase::kIdle;
            emu_.Get<IrqController>().PulseIrq(kVicLine);
            return;
        }
        nacked_      = false;
        addr_        = addr;
        slave_index_ = index;
        I2cSlave& slave = Slave();
        if (!restart) slave.BusStart();
        slave.TxnStart(static_cast<uint8_t>(addr_));
        emu_.Get<IrqController>().PulseIrq(kVicLine);
        if (read) {
            phase_        = Phase::kReceive;
            last_pending_ = false;
            BeginReception();
            return;
        }
        phase_ = Phase::kTransmit;
        if (last) Stop();
    }

    uint32_t ReadData() {
        if (!rx_held_) {
            emu_.Get<Fatal>().Die(
                "Peripheral '%s': the guest read READ_DATA with no received byte held",
                typeid(*this).name());
        }
        rx_held_ = false;
        if (!rx_final_ && phase_ == Phase::kReceive) BeginReception();
        return rx_byte_;
    }

    void BeginReception() {
        const uint64_t core_khz = emu_.Get<Msm8255ClockRates>().ReportClockFreqKhz(kCoreClock);
        const uint64_t divider = (clk_ctl_ & kClkCtlFsDivider) + kFsDividerBias;
        const int64_t  ns = static_cast<int64_t>(
            kClocksPerByte * 2u * divider * kNsPerKhzPeriod / core_khz);
        auto& clock = emu_.Get<GuestCycleClock>();
        rx_busy_ = true;
        rx_due_  = clock.CycleAfterNs(ns);
        clock.Arm(rx_event_, rx_due_);
    }

    void CompleteReception() {
        if (!rx_busy_) return;
        rx_busy_  = false;
        rx_byte_  = Slave().TxnReadByte(static_cast<uint8_t>(addr_));
        rx_held_  = true;
        rx_final_ = last_pending_;
        emu_.Get<IrqController>().PulseIrq(kVicLine);
        if (rx_final_) {
            last_pending_ = false;
            Stop();
        }
    }

    void Stop() {
        phase_ = Phase::kIdle;
        Slave().BusStop();
    }

    I2cSlave& Slave() {
        const auto slaves = emu_.Get<Msm8255I2cSlaves>().Slaves(kBus);
        if (slave_index_ >= slaves.size()) {
            emu_.Get<Fatal>().Die("Peripheral '%s': no slave acknowledged the transfer to 0x%02X on "
                                  "bus %u", typeid(*this).name(), addr_, kBus);
        }
        return *slaves[slave_index_];
    }

    void ResetController() {
        emu_.Get<GuestCycleClock>().Disarm(rx_event_);
        clk_ctl_      = 0u;
        intf_select_  = 0u;
        phase_        = Phase::kIdle;
        addr_         = 0u;
        rx_byte_      = 0u;
        rx_held_      = false;
        rx_final_     = false;
        rx_busy_      = false;
        last_pending_ = false;
        rx_due_       = 0u;
        nacked_       = false;
        slave_index_  = kNoSlave;
    }

    GuestCycleClock::Event* rx_event_    = nullptr;
    Msm8255ClockReset*      clock_reset_ = nullptr;

    uint32_t clk_ctl_      = 0u;
    uint32_t intf_select_  = 0u;
    Phase    phase_        = Phase::kIdle;
    uint32_t addr_         = 0u;
    uint32_t rx_byte_      = 0u;
    bool     rx_held_      = false;
    bool     rx_final_     = false;
    bool     rx_busy_      = false;
    bool     last_pending_ = false;
    uint64_t rx_due_       = 0u;
    bool     nacked_       = false;
    uint32_t slave_index_  = kNoSlave;
};

}

}
