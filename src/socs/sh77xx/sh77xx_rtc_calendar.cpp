#include "sh77xx_rtc_calendar.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <iterator>

namespace cerfcore {

namespace {

constexpr BcdCalendar::Field kCounterFields[Sh77xxRtcCalendar::kCounterCount] = {
    {0x7Fu, 0u, 59u}, {0x7Fu, 0u, 59u}, {0x3Fu, 0u, 23u}, {0x07u, 0u, 6u},
    {0x3Fu, 1u, 31u}, {0x1Fu, 0u, 12u}, {0xFFu, 0u, 99u},
};

constexpr BcdCalendar::Field kAlarmFields[Sh77xxRtcCalendar::kAlarmFieldCount] = {
    {0x7Fu, 0u, 59u}, {0x7Fu, 0u, 59u}, {0x3Fu, 0u, 23u}, {0x07u, 0u, 6u}, {0x3Fu, 1u, 31u}, {0x1Fu, 1u, 12u},
};

constexpr const char* kStateNames[Sh77xxRtcCalendar::kCounterCount] = {
    "cal_sec", "cal_min", "cal_hour", "cal_weekday", "cal_date", "cal_month", "cal_year",
};

constexpr uint32_t kSecondsPerMinute = 60u;
constexpr uint32_t kRoundUpSecond    = 30u;
constexpr uint32_t kDaysPerWeek      = 7u;
constexpr uint32_t kYearsPerCounter  = 100u;
constexpr uint64_t kSecondsPerDay    = 86400u;
constexpr uint64_t kCounterCycleDays = 36525u;
constexpr uint64_t kOnsetSearchLimit = kDaysPerWeek * kCounterCycleDays * kSecondsPerDay;

constexpr uint32_t kMismatchOrder[Sh77xxRtcCalendar::kAlarmFieldCount] = {
    Sh77xxRtcCalendar::kMonth, Sh77xxRtcCalendar::kDate,   Sh77xxRtcCalendar::kWeekday,
    Sh77xxRtcCalendar::kHour,  Sh77xxRtcCalendar::kMinute, Sh77xxRtcCalendar::kSecond,
};

}

uint8_t Sh77xxRtcCalendar::FieldMask(uint32_t counter) {
    return static_cast<uint8_t>(kCounterFields[counter].mask);
}

bool Sh77xxRtcCalendar::AlarmFieldValid(uint32_t field, uint8_t value) {
    return (value & kAlarmEnable) == 0u || IsCount(kAlarmFields[field], value);
}

bool Sh77xxRtcCalendar::AlarmEnabled(const Alarm& alarm) {
    return std::any_of(alarm.begin(), alarm.end(), [](uint8_t field) { return (field & kAlarmEnable) != 0u; });
}

void Sh77xxRtcCalendar::SeedFromHost() {
    const HostDate host = HostNow();
    value_[kSecond]     = host.sec;
    value_[kMinute]     = host.min;
    value_[kHour]       = host.hour;
    value_[kWeekday]    = host.wday;
    value_[kDate]       = host.date;
    value_[kMonth]      = host.month;
    value_[kYear]       = host.year % kYearsPerCounter;
}

uint8_t Sh77xxRtcCalendar::Register(Counter counter) const { return static_cast<uint8_t>(ToBcd(value_[counter])); }

bool Sh77xxRtcCalendar::Write(Counter counter, uint8_t value) {
    uint32_t decoded = 0u;
    if (!DecodeField(kCounterFields[counter], value, decoded)) return false;
    value_[counter] = decoded;
    return true;
}

bool Sh77xxRtcCalendar::DateFitsMonth() const { return value_[kDate] <= DaysInMonth(value_[kMonth], value_[kYear]); }

bool Sh77xxRtcCalendar::CrossesDay(uint64_t secs) const {
    return secs >= SecondsToNextDay(value_[kHour], value_[kMinute], value_[kSecond]);
}

void Sh77xxRtcCalendar::Advance(uint64_t secs) {
    const uint64_t days = AddSeconds(value_[kSecond], value_[kMinute], value_[kHour], secs);
    value_[kWeekday]    = static_cast<uint32_t>((value_[kWeekday] + days % kDaysPerWeek) % kDaysPerWeek);
    for (uint64_t day = 0u; day < days; ++day) NextDate(value_[kDate], value_[kMonth], value_[kYear]);
}

uint64_t Sh77xxRtcCalendar::RoundUpSeconds() const {
    return value_[kSecond] < kRoundUpSecond ? 0u : kSecondsPerMinute - value_[kSecond];
}

void Sh77xxRtcCalendar::RoundToMinute() {
    const uint64_t up = RoundUpSeconds();
    if (up == 0u) {
        value_[kSecond] = 0u;
        return;
    }
    Advance(up);
}

bool Sh77xxRtcCalendar::Matches(const Alarm& alarm) const {
    return AlarmEnabled(alarm) && CoarsestMismatch(alarm) == kAlarmFieldCount;
}

uint32_t Sh77xxRtcCalendar::CoarsestMismatch(const Alarm& alarm) const {
    for (const uint32_t field : kMismatchOrder) {
        if ((alarm[field] & kAlarmEnable) == 0u) continue;
        if ((alarm[field] & kAlarmFields[field].mask) != ToBcd(value_[field])) return field;
    }
    return kAlarmFieldCount;
}

uint64_t Sh77xxRtcCalendar::SecondsToFieldEnd(uint32_t field) const {
    switch (field) {
    case kSecond: return 1u;
    case kMinute: return SecondsToNextMinute(value_[kSecond]);
    case kHour: return SecondsToNextHour(value_[kMinute], value_[kSecond]);
    case kWeekday:
    case kDate: return SecondsToNextDay(value_[kHour], value_[kMinute], value_[kSecond]);
    }
    return SecondsToNextDay(value_[kHour], value_[kMinute], value_[kSecond]) +
           static_cast<uint64_t>(DaysInMonth(value_[kMonth], value_[kYear]) - value_[kDate]) * kSecondsPerDay;
}

uint64_t Sh77xxRtcCalendar::SecondsToOnset(const Alarm& alarm, bool matching) const {
    if (!AlarmEnabled(alarm)) return 0u;
    Sh77xxRtcCalendar c     = *this;
    uint64_t          total = 0u;
    if (matching) {
        const auto finest = std::find_if(std::rbegin(kMismatchOrder), std::rend(kMismatchOrder),
                                         [&](uint32_t field) { return (alarm[field] & kAlarmEnable) != 0u; });
        total             = c.SecondsToFieldEnd(*finest);
        c.Advance(total);
    }
    while (total <= kOnsetSearchLimit) {
        const uint32_t field = c.CoarsestMismatch(alarm);
        if (field == kAlarmFieldCount) return total;
        const uint64_t step = c.SecondsToFieldEnd(field);
        c.Advance(step);
        total += step;
    }
    return 0u;
}

void Sh77xxRtcCalendar::Save(StateWriter& w) const {
    for (uint32_t counter = 0u; counter < kCounterCount; ++counter) w.Write(kStateNames[counter], value_[counter]);
}

void Sh77xxRtcCalendar::Restore(StateReader& r) {
    for (uint32_t counter = 0u; counter < kCounterCount; ++counter) r.Read(kStateNames[counter], value_[counter]);
}

}
