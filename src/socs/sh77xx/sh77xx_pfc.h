#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/core/callback_list.h>

namespace cerfcore {

inline constexpr uint32_t kSh77xxPortCount = 12u;
inline constexpr uint32_t kSh77xxPortPins  = 8u;
inline constexpr uint32_t kSh77xxPortC     = 2u;
inline constexpr uint32_t kSh77xxPortD     = 3u;
inline constexpr uint32_t kSh77xxPortE     = 4u;
inline constexpr uint32_t kSh77xxPortF     = 5u;
inline constexpr uint32_t kSh77xxPortG     = 6u;
inline constexpr uint32_t kSh77xxPortH     = 7u;
inline constexpr uint32_t kSh77xxPortJ     = 8u;
inline constexpr uint32_t kSh77xxPortK     = 9u;
inline constexpr uint32_t kSh77xxPortL     = 10u;
inline constexpr uint32_t kSh77xxPortSc    = 11u;

inline constexpr uint32_t kSh77xxPinModeOther       = 0u;
inline constexpr uint32_t kSh77xxPinModeOutput      = 1u;
inline constexpr uint32_t kSh77xxPinModeInputFirst  = 2u;
inline constexpr uint32_t kSh77xxPinModePullUpInput = 2u;

class Sh77xxPfc : public Peripheral {
public:
    using Peripheral::Peripheral;

    uint32_t PinMode(uint32_t port, uint32_t pin) const { return (PortControl(port) >> (pin * 2u)) & 3u; }

    void AddControlListener(std::function<void()> listener) { listeners_.Add(std::move(listener)); }

protected:
    virtual uint16_t PortControl(uint32_t port) const = 0;

    void NotifyControlChanged() {
        listeners_.Call();
    }

private:
    CallbackList<> listeners_;
};

}
