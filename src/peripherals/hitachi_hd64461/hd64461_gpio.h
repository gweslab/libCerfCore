#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "../../socs/reg_window_impl.h"
#include "../pin_drive.h"

namespace cerfcore {

class Hd64461GpioWiring;

namespace cerf_hd64461_gpio_detail {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

constexpr uint32_t kPorts     = 4u;
constexpr uint32_t kPins      = 8u;
constexpr uint32_t kDataIndex = 8u;

constexpr RegSpec Control() {
    return RegSpec{ReadKind::kStored, WriteKind::kStored, 0xFFFFu, 0xAAAAu, 0x0000u, OtherReset::kRetain,
                   0x0000u, 0x0000u};
}

constexpr RegSpec Data() {
    return RegSpec{ReadKind::kSampled, WriteKind::kStored, 0x00FFu, 0x0000u, 0x0000u, OtherReset::kRetain,
                   0x0000u, 0x0000u, true};
}

constexpr RegWindowModel Window() {
    RegWindowModel w{0x10004000u, 0x48u, 36u, false, {}};
    for (uint32_t port = 0; port < kPorts; ++port) {
        w.reg[port]              = Control();
        w.reg[kDataIndex + port] = Data();
    }
    return w;
}

inline constexpr RegWindowModel kModel = Window();

}

class Hd64461Gpio
    : public cerf_reg_window_detail::RegWindow<cerf_hd64461_gpio_detail::kModel, Peripheral, std::mutex> {
public:
    static constexpr uint32_t kPortA = 0u;
    static constexpr uint32_t kPortB = 1u;
    static constexpr uint32_t kPortC = 2u;
    static constexpr uint32_t kPortD = 3u;

    using RegWindow::RegWindow;

    bool ShouldRegister() override;
    void OnReady() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    PinDrive Drive(uint32_t port, uint32_t pin) const;
    bool     Pcmcia1PinsConnected() const;
    void     AddPinListener(std::function<void()> listener);

protected:
    uint16_t SampleRegister(uint32_t i, uint32_t addr) override;
    void     BeforeStore(uint32_t i, uint16_t value) override;
    void     AfterStore(uint32_t i) override;
    void     AfterReset() override;

private:
    uint8_t ModePins(uint16_t control, uint32_t mode) const;
    uint8_t Pcmcia1Inputs(uint32_t port) const;
    uint8_t Pcmcia1InputLevels(uint32_t port);
    void    PublishLocked();
    void    NotifyListeners();

    Hd64461GpioWiring*                 wiring_ = nullptr;
    std::atomic<uint32_t>              pins_[cerf_hd64461_gpio_detail::kPorts]{};
    std::vector<std::function<void()>> listeners_;
};

}
