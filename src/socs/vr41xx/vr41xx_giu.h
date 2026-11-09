#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <functional>
#include <optional>

namespace cerfcore {

class StateWriter;
class StateReader;

enum class Vr41xxPinDrive : uint8_t { kLow, kHigh, kUndriven };

/* NEC VR41xx GIU (General Purpose I/O Unit): the GPIO direction/data registers plus
   the Level-2 GPIO interrupt block, whose output the ICU takes as GIUINTLREG/
   GIUINTHREG (VR4121 UM ch.19, VR4102 UM ch.18, VR4131 UM ch.14). */
class Vr41xxGiu : public Peripheral {
public:
    static constexpr int kDcdPin = 15;

    using Peripheral::Peripheral;

    /* GPIO(31:0) pins (VR4121 UM 19.2.1/19.2.2, VR4102 UM 18.2.1/18.2.2). */
    virtual void SetPinLevel(int pin, bool level) = 0;
    virtual void ReleasePin(int pin) = 0;

    virtual bool GetPinLevel(int pin) = 0;

    virtual uint16_t OutputDataLow() = 0;
    virtual void     RegisterOutputDataLowListener(std::function<void(uint16_t previous)> fn) = 0;

    using AlternateFunction = std::function<std::optional<Vr41xxPinDrive>(int gpio)>;
    virtual void RegisterAlternateFunction(int first_gpio, int last_gpio, AlternateFunction fn) = 0;
    virtual Vr41xxPinDrive OutputDrive(int gpio) = 0;
    virtual void RegisterOutputDriveListener(std::function<void()> fn) = 0;
    virtual void NotifyOutputDriveChanged() = 0;

    virtual void RegisterPinDeviceState(std::function<void(StateWriter&)> save,
                                        std::function<void(StateReader&)> restore) = 0;

    virtual uint16_t ReadPullRegister(uint32_t off)                  = 0;
    virtual void     WritePullRegister(uint32_t off, uint16_t value) = 0;
};

}
