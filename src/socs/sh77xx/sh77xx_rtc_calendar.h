#pragma once

#include "../../peripherals/rtc/bcd_calendar.h"

#include <array>
#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Sh77xxRtcCalendar : public BcdCalendar {
public:
    enum Counter : uint8_t { kSecond, kMinute, kHour, kWeekday, kDate, kMonth, kYear, kCounterCount };

    static constexpr uint32_t kAlarmFieldCount = kYear;
    static constexpr uint8_t  kAlarmEnable     = 0x80u;

    using Alarm = std::array<uint8_t, kAlarmFieldCount>;

    static uint8_t FieldMask(uint32_t counter);
    static bool    AlarmFieldValid(uint32_t field, uint8_t value);
    static bool    AlarmEnabled(const Alarm& alarm);

    void     SeedFromHost();
    uint32_t Value(Counter counter) const { return value_[counter]; }
    uint8_t  Register(Counter counter) const;
    bool     Write(Counter counter, uint8_t value);
    bool     DateFitsMonth() const;
    bool     CrossesDay(uint64_t secs) const;
    void     Advance(uint64_t secs);
    uint64_t RoundUpSeconds() const;
    void     RoundToMinute();
    bool     Matches(const Alarm& alarm) const;
    uint64_t SecondsToOnset(const Alarm& alarm, bool matching) const;

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    uint32_t CoarsestMismatch(const Alarm& alarm) const;
    uint64_t SecondsToFieldEnd(uint32_t field) const;

    std::array<uint32_t, kCounterCount> value_{0u, 0u, 0u, 0u, 1u, 1u, 0u};
};

}
