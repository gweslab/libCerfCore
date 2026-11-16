#pragma once

#include "stm32f2_mcu.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class GpioPins {
public:
    virtual ~GpioPins() = default;
    virtual bool ExternalLevel(uint32_t port, uint32_t pin, bool* level) = 0;
    virtual bool AlternateLevel(uint32_t port, uint32_t pin, uint32_t af, bool* level) = 0;
    virtual void OutputChanged(uint32_t port, uint32_t pin, bool driven, bool level) = 0;
};

class Gpio : public Peripheral {
public:
    static constexpr uint32_t kBaseA = 0x40020000u;
    static constexpr uint32_t kStride = 0x400u;
    static constexpr uint32_t kPorts = 9u;

    Gpio(Mcu& mcu, GpioPins& pins, uint32_t port);

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

    bool PinLevel(uint32_t pin);
    bool DrivenLevel(uint32_t pin, bool* level);
    uint32_t Mode(uint32_t pin) const { return (moder_ >> (2u * pin)) & 3u; }
    bool Driven(uint32_t pin) const { return Mode(pin) == 1u && (((otyper_ & odr_) >> pin) & 1u) == 0u; }
    uint32_t DrivenMask() const;
    uint32_t Pull(uint32_t pin) const { return (pupdr_ >> (2u * pin)) & 3u; }
    uint32_t AlternateFunction(uint32_t pin) const {
        return ((pin < 8u ? afrl_ : afrh_) >> (4u * (pin & 7u))) & 0xFu;
    }

private:
    uint32_t InputData();
    void NotifyOutputs(uint32_t old_driven, uint32_t old_odr);

    Mcu& mcu_;
    GpioPins& pins_;
    uint32_t port_;
    uint32_t moder_ = 0u;
    uint32_t otyper_ = 0u;
    uint32_t ospeedr_ = 0u;
    uint32_t pupdr_ = 0u;
    uint32_t odr_ = 0u;
    uint32_t afrl_ = 0u;
    uint32_t afrh_ = 0u;
    uint32_t floating_reported_ = 0u;
};

}

}
