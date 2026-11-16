#include "../../socs/i2c_slave.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include "../../execution/guest_cycle_clock.h"
#include "../rtc/bcd_calendar.h"
#include "epson_rx8571_sa_wiring.h"

#include <cstdint>

namespace cerfcore {

namespace {

uint8_t BinToBcd(int value) {
    return static_cast<uint8_t>(BcdCalendar::ToBcd(static_cast<uint32_t>(value)));
}

bool BcdToBin(uint8_t value, uint8_t mask, int maximum, int& result) {
    uint32_t decoded = 0;
    if (!BcdCalendar::DecodeBcd(value & mask, 0u, static_cast<uint32_t>(maximum), decoded)) return false;
    result = static_cast<int>(decoded);
    return true;
}

constexpr uint8_t kRegYear = 0x06u;
constexpr uint8_t kRegFlag = 0x0Eu;
constexpr uint8_t kRegControl = 0x0Fu;
constexpr uint8_t kFlagVlf = 0x02u;
constexpr uint8_t kFlagUf = 0x20u;
constexpr uint8_t kFlagAf = 0x08u;
constexpr uint8_t kFlagTf = 0x10u;
constexpr uint8_t kFlagTest = 0xC4u;
constexpr uint8_t kControlStop = 0x02u;
constexpr uint8_t kFlagWriteMask = 0xFEu;
constexpr uint8_t kClockWriteMasks[7] = {0x7Fu, 0x7Fu, 0x3Fu, 0x7Fu, 0x3Fu, 0x1Fu, 0xFFu};
constexpr uint8_t kExtensionUpdateSelect = 0x20u;
constexpr uint8_t kLastClockRegister = 0x06u;
constexpr uint8_t kExtensionRegisterInitialStub = 0x04u;
constexpr uint8_t kRamRegisterInitialStub = 0x00u;
constexpr uint8_t kAlarmRegisterInitialStub = 0x00u;
constexpr uint8_t kTimerRegisterInitialStub = 0x00u;
constexpr uint8_t kFlagRegisterInitialStub = 0x00u;
constexpr uint8_t kControlRegisterInitialStub = 0x00u;
constexpr int64_t kNsPerSecond = 1'000'000'000;
constexpr const char* kRegisterFieldNames[16] = {
    "second", "minute", "hour", "weekday", "day", "month", "year", "register7",
    "register8", "register9", "register10", "register11", "register12", "register13", "flag", "control"};

class EpsonRx8571Sa final : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override { return emu_.TryGet<EpsonRx8571SaWiring>() != nullptr; }

    void OnReady() override {
        clock_ = &emu_.Get<GuestCycleClock>();
        sleep_ = &emu_.Get<GuestDeepSleep>();
        SeedFromHost();
        MaterializeClockRegisters();
        emu_.Get<EpsonRx8571SaWiring>().Attach(this);
    }

    bool MatchesAddress(uint8_t slave_addr) const override {
        return slave_addr == emu_.Get<EpsonRx8571SaWiring>().SlaveAddress();
    }

    void TxnStart(uint8_t) override {
        EnsureAnchored();
        if (!Stopped()) MaterializeClockRegisters();
        start_pending_ = true;
    }

    void TxnWriteByte(uint8_t, uint8_t value) override {
        if (start_pending_) {
            start_pending_ = false;
            expecting_pointer_ = true;
        }
        if (expecting_pointer_) {
            pointer_ = value;
            expecting_pointer_ = false;
            return;
        }
        if (pointer_ > 0x1Fu)
            emu_.Get<Fatal>().Die("RX-8571SA register address 0x%02X is not modelled", pointer_);
        const uint8_t index = pointer_;
        if (index >= 0x10u) {
            user_ram_[index - 0x10u] = value;
            AdvancePointer();
            return;
        }
        if (index > kLastClockRegister && index != kRegFlag &&
            (index != kRegControl || (value & static_cast<uint8_t>(~kControlStop)) != 0u))
            emu_.Get<Fatal>().Die("RX-8571SA register 0x%02X write 0x%02X is not modelled", index, value);
        if (index <= kLastClockRegister && (value & static_cast<uint8_t>(~kClockWriteMasks[index])) != 0u)
            emu_.Get<Fatal>().Die("RX-8571SA %s register write 0x%02X sets reserved bits",
                                  kRegisterFieldNames[index], value);
        const bool was_stopped = Stopped();
        if (index <= kLastClockRegister && !was_stopped)
            emu_.Get<Fatal>().Die("RX-8571SA clock register 0x%02X write 0x%02X with STOP clear is not "
                                  "modelled", index, value);
        if (index == kRegFlag) {
            WriteFlag(value);
            AdvancePointer();
            return;
        }
        if (index == kRegControl && !was_stopped && (value & kControlStop) != 0u) FreezeClock();
        registers_[index] = index == 3u ? static_cast<uint8_t>(value & 0x7Fu) : value;
        if (index == kRegControl && was_stopped && !Stopped()) CommitClockRegisters();
        AdvancePointer();
    }

    uint8_t TxnReadByte(uint8_t) override {
        if (start_pending_) {
            start_pending_ = false;
            expecting_pointer_ = false;
        }
        if (pointer_ > 0x1Fu)
            emu_.Get<Fatal>().Die("RX-8571SA register address 0x%02X is not modelled", pointer_);
        const uint8_t value = pointer_ >= 0x10u ? user_ram_[pointer_ - 0x10u] : registers_[pointer_];
        AdvancePointer();
        return value;
    }

    void SaveState(StateWriter& writer) override {
        EnsureAnchored();
        if (!Stopped()) MaterializeClockRegisters();
        for (uint32_t i = 0; i < 16u; ++i)
            writer.Write(kRegisterFieldNames[i], registers_[i]);
        writer.WriteBytes("user_ram", user_ram_, sizeof(user_ram_));
        writer.Write("pointer", pointer_);
        writer.Write("counter_second", second_);
        writer.Write("counter_minute", minute_);
        writer.Write("counter_hour", hour_);
        writer.Write("counter_date", date_);
        writer.Write("counter_month", month_);
        writer.Write("counter_year", year_);
        writer.Write("counter_fraction_ns", counter_fraction_ns_);
        writer.Write("frozen_fraction_ns", frozen_fraction_ns_);
        writer.Write("expecting_pointer", static_cast<uint8_t>(expecting_pointer_));
        writer.Write("start_pending", static_cast<uint8_t>(start_pending_));
    }

    void RestoreState(StateReader& reader) override {
        for (uint32_t i = 0; i < 16u; ++i)
            reader.Read(kRegisterFieldNames[i], registers_[i]);
        reader.ReadBytes("user_ram", user_ram_, sizeof(user_ram_));
        reader.Read("pointer", pointer_);
        reader.Read("counter_second", second_);
        reader.Read("counter_minute", minute_);
        reader.Read("counter_hour", hour_);
        reader.Read("counter_date", date_);
        reader.Read("counter_month", month_);
        reader.Read("counter_year", year_);
        reader.Read("counter_fraction_ns", counter_fraction_ns_);
        reader.Read("frozen_fraction_ns", frozen_fraction_ns_);
        uint8_t expecting_pointer = 0;
        reader.Read("expecting_pointer", expecting_pointer);
        expecting_pointer_ = expecting_pointer != 0u;
        uint8_t start_pending = 0;
        reader.Read("start_pending", start_pending);
        start_pending_ = start_pending != 0u;
        anchor_pending_ = true;
    }

private:
    void AdvancePointer() {
        if (pointer_ == 0x0Fu) {
            pointer_ = 0u;
            return;
        }
        if (pointer_ == 0x1Fu)
            emu_.Get<Fatal>().Die("RX-8571SA auto-increment beyond register 0x1F is not modelled");
        ++pointer_;
    }

    bool Stopped() const { return (registers_[kRegControl] & kControlStop) != 0; }

    void Anchor() {
        anchor_ns_ = clock_->NowNs() - counter_fraction_ns_;
        anchor_slept_ns_ = sleep_->SleptNs();
    }

    void EnsureAnchored() {
        if (!anchor_pending_) return;
        anchor_pending_ = false;
        Anchor();
    }

    int64_t ElapsedNs() const {
        if (Stopped()) return frozen_fraction_ns_;
        return (clock_->NowNs() - anchor_ns_) + (sleep_->SleptNs() - anchor_slept_ns_);
    }

    void FreezeClock() {
        MaterializeClockRegisters();
        frozen_fraction_ns_ = counter_fraction_ns_;
    }

    void MaterializeClockRegisters() {
        const int64_t elapsed = ElapsedNs();
        const uint64_t elapsed_seconds = static_cast<uint64_t>(elapsed / kNsPerSecond);
        const int64_t fraction = elapsed % kNsPerSecond;
        if (elapsed_seconds != 0u) {
            const uint32_t old_second = second_;
            const bool minute_event = elapsed_seconds >= 60u - old_second;
            const uint64_t day_carries = BcdCalendar::AddSeconds(second_, minute_, hour_, elapsed_seconds);
            for (uint64_t i = 0; i < day_carries; ++i) {
                BcdCalendar::NextDate(date_, month_, year_);
                uint8_t week = static_cast<uint8_t>(registers_[3] & 0x7Fu);
                registers_[3] = static_cast<uint8_t>(((week << 1) | (week >> 6)) & 0x7Fu);
            }
            const bool minute_selected = (registers_[0x0Du] & kExtensionUpdateSelect) != 0u;
            if ((!minute_selected || minute_event) && !Stopped()) registers_[kRegFlag] |= kFlagUf;
        }
        counter_fraction_ns_ = fraction;
        registers_[0] = BinToBcd(static_cast<int>(second_));
        registers_[1] = BinToBcd(static_cast<int>(minute_));
        registers_[2] = BinToBcd(static_cast<int>(hour_));
        registers_[4] = BinToBcd(static_cast<int>(date_));
        registers_[5] = BinToBcd(static_cast<int>(month_));
        registers_[kRegYear] = BinToBcd(static_cast<int>(year_));
        Anchor();
    }

    void CommitClockRegisters() {
        int second = 0, minute = 0, hour = 0, day = 0, month = 0, year2 = 0;
        if (!BcdToBin(registers_[0], 0x7Fu, 59, second) || !BcdToBin(registers_[1], 0x7Fu, 59, minute) ||
            !BcdToBin(registers_[2], 0x3Fu, 23, hour) || !BcdToBin(registers_[4], 0x3Fu, 31, day) || day < 1 ||
            !BcdToBin(registers_[5], 0x1Fu, 12, month) || month < 1 ||
            !BcdToBin(registers_[kRegYear], 0xFFu, 99, year2)) {
            emu_.Get<Fatal>().Die("RX-8571SA clock registers hold a value that is not BCD");
        }
        uint32_t week = registers_[3];
        if ((week & 0x80u) != 0u || week == 0u || (week & static_cast<uint8_t>(week - 1u)) != 0u)
            emu_.Get<Fatal>().Die("RX-8571SA week register 0x%02X is not a one-hot day", week);
        second_ = static_cast<uint32_t>(second);
        minute_ = static_cast<uint32_t>(minute);
        hour_ = static_cast<uint32_t>(hour);
        date_ = static_cast<uint32_t>(day);
        month_ = static_cast<uint32_t>(month);
        year_ = static_cast<uint32_t>(year2);
        if (date_ > BcdCalendar::DaysInMonth(month_, year_))
            emu_.Get<Fatal>().Die("RX-8571SA date %u is invalid for month %u year %u", date_, month_, year_);
        frozen_fraction_ns_ = 0;
        counter_fraction_ns_ = 0;
        Anchor();
    }

    void SeedFromHost() {
        const BcdCalendar::HostDate date = BcdCalendar::HostNow();
        second_ = date.sec;
        minute_ = date.min;
        hour_ = date.hour;
        date_ = date.date;
        month_ = date.month;
        year_ = date.year % 100u;
        registers_[3] = static_cast<uint8_t>(1u << date.wday);
        Anchor();
    }

    void WriteFlag(uint8_t value) {
        if ((value & kFlagTest) != 0u)
            emu_.Get<Fatal>().Die("RX-8571SA flag register write 0x%02X sets a write-prohibited bit", value);
        if ((value & static_cast<uint8_t>(~kFlagWriteMask)) != 0u)
            emu_.Get<Fatal>().Die("RX-8571SA flag register write 0x%02X sets a write-prohibited bit", value);
        const uint8_t clearable = static_cast<uint8_t>(kFlagUf | kFlagAf | kFlagTf);
        registers_[kRegFlag] &= static_cast<uint8_t>(value | static_cast<uint8_t>(~clearable));
        if ((value & kFlagVlf) == 0u) registers_[kRegFlag] &= static_cast<uint8_t>(~kFlagVlf);
        registers_[kRegFlag] &= static_cast<uint8_t>(~kFlagTest);
    }

    uint8_t registers_[16] = {0, 0, 0, 0, 0, 0, 0, kRamRegisterInitialStub,
                              kAlarmRegisterInitialStub, kAlarmRegisterInitialStub, kAlarmRegisterInitialStub,
                              kTimerRegisterInitialStub, kTimerRegisterInitialStub,
                              kExtensionRegisterInitialStub, kFlagRegisterInitialStub,
                              kControlRegisterInitialStub};
    uint8_t user_ram_[16] = {};
    uint8_t pointer_ = 0;
    bool expecting_pointer_ = true;
    bool start_pending_ = false;
    GuestCycleClock* clock_ = nullptr;
    GuestDeepSleep* sleep_ = nullptr;
    uint32_t second_ = 0;
    uint32_t minute_ = 0;
    uint32_t hour_ = 0;
    uint32_t date_ = 1;
    uint32_t month_ = 1;
    uint32_t year_ = 0;
    int64_t anchor_ns_ = 0;
    int64_t anchor_slept_ns_ = 0;
    int64_t counter_fraction_ns_ = 0;
    int64_t frozen_fraction_ns_ = 0;
    bool anchor_pending_ = false;
};

}

REGISTER_SERVICE(EpsonRx8571Sa);

}
