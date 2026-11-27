#pragma once

#include <cerfcore/core/service.h>
#include "msm8255_gpio_banks.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfcore {

enum class Msm8255GpioOutput : uint8_t { kFloating, kLow, kHigh };

class Msm8255GpioWindow {
public:
    virtual ~Msm8255GpioWindow() = default;

    virtual bool     OwnsGpioPin(uint32_t pin) const     = 0;
    virtual uint32_t GpioPinFunction(uint32_t pin) const = 0;
    virtual void     SetGpioInputPin(uint32_t pin, bool high) = 0;
    virtual void ApplyModemPinConfig(uint32_t pin, uint32_t mux_config, bool output) = 0;
};

class Msm8255GpioBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void RegisterWindow(Msm8255GpioWindow* window);

    void SetInputPin(uint32_t pin, bool high);
    void RestoreInputPin(uint32_t pin, bool high);

    uint32_t PinFunction(uint32_t pin);

    using InputListener = std::function<void(bool)>;

    void AddInputListener(uint32_t pin, InputListener listener);

    void ApplyModemPinConfig(uint32_t pin, uint32_t mux_config, bool output);

    void RedriveOwnedPins(Msm8255GpioWindow* window);

    using OutputListener = std::function<void(Msm8255GpioOutput)>;

    void AddOutputListener(uint32_t pin, OutputListener listener);

    void PublishOutputs(uint32_t first_pin, uint32_t span, uint32_t enables, uint32_t levels);

private:
    static constexpr uint32_t kPinCount = kMsm8255GpioPinCount;

    enum : uint8_t { kUndriven = 0u, kDrivenLow = 1u, kDrivenHigh = 2u };

    struct OutputWatch {
        uint32_t          pin;
        Msm8255GpioOutput last;
        OutputListener    listener;
    };

    struct InputWatch {
        uint32_t      pin;
        InputListener listener;
    };

    void DriveInputPin(uint32_t pin, bool high, bool notify_edge);
    void RedriveOwnedPinsLocked(Msm8255GpioWindow* window);
    Msm8255GpioWindow* OwnerLocked(uint32_t pin) const;

    mutable std::mutex                            mtx_;
    std::vector<Msm8255GpioWindow*>               windows_;
    std::array<std::atomic<uint8_t>, kPinCount>   driven_{};
    std::vector<OutputWatch>                      output_watches_;
    std::vector<InputWatch>                       input_watches_;
};

}
