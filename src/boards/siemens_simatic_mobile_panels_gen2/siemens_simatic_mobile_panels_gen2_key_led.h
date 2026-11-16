#pragma once

#include <cerfcore/host/machine_indicators.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace cerfcore {

class SiemensSimaticMobilePanelsGen2KeyLedOwner {
public:
    virtual bool KeyLedLit(uint32_t line) const = 0;

protected:
    ~SiemensSimaticMobilePanelsGen2KeyLedOwner() = default;
};

class SiemensSimaticMobilePanelsGen2KeyLed final : public MachineLamp {
public:
    SiemensSimaticMobilePanelsGen2KeyLed(const SiemensSimaticMobilePanelsGen2KeyLedOwner& owner, uint32_t line,
                                         uint8_t number, ObjectRgb color)
        : owner_(owner), line_(line), number_(number), color_(color) {}

    std::string ObjectId() const override { return "key_led_" + std::to_string(number_); }

    static constexpr char kObjectNameFormat[] = "Key LED %u";
    std::string ObjectName() const override {
        char name[sizeof(kObjectNameFormat) + 8];
        std::snprintf(name, sizeof(name), kObjectNameFormat, static_cast<unsigned>(number_));
        return name;
    }

    LampState Lamp() const override { return LampState{owner_.KeyLedLit(line_), false, color_}; }

    using MachineLamp::Refresh;

private:
    const SiemensSimaticMobilePanelsGen2KeyLedOwner& owner_;
    uint32_t                                          line_;
    uint8_t                                           number_;
    ObjectRgb                                         color_;
};

}
