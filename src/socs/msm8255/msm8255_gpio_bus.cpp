#include "msm8255_gpio_bus.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <utility>

namespace cerfcore {

bool Msm8255GpioBus::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255GpioBus::RegisterWindow(Msm8255GpioWindow* window) {
    std::lock_guard<std::mutex> lk(mtx_);
    windows_.push_back(window);
    RedriveOwnedPinsLocked(window);
}

void Msm8255GpioBus::SetInputPin(uint32_t pin, bool high) { DriveInputPin(pin, high, true); }

void Msm8255GpioBus::RestoreInputPin(uint32_t pin, bool high) { DriveInputPin(pin, high, false); }

void Msm8255GpioBus::DriveInputPin(uint32_t pin, bool high, bool notify_edge) {
    if (pin >= kPinCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 gpio bus: gpio %u is past the %u pins the controller "
            "addresses", pin, kPinCount);
    }

    const uint8_t level = high ? kDrivenHigh : kDrivenLow;
    const bool    edge  = driven_[pin].exchange(level, std::memory_order_acq_rel) != level;

    std::vector<const InputListener*> notify;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (Msm8255GpioWindow* owner = OwnerLocked(pin)) owner->SetGpioInputPin(pin, high);
        if (edge && notify_edge) {
            for (const InputWatch& watch : input_watches_) {
                if (watch.pin == pin) notify.push_back(&watch.listener);
            }
        }
    }
    for (const InputListener* listener : notify) (*listener)(high);
}

uint32_t Msm8255GpioBus::PinFunction(uint32_t pin) {
    std::lock_guard<std::mutex> lk(mtx_);
    Msm8255GpioWindow* owner = OwnerLocked(pin);
    if (owner == nullptr) {
        emu_.Get<Fatal>().Die("msm8255 gpio bus: the function of gpio %u, which no window serves",
                              pin);
    }
    return owner->GpioPinFunction(pin);
}

void Msm8255GpioBus::AddInputListener(uint32_t pin, InputListener listener) {
    if (pin >= kPinCount) {
        emu_.Get<Fatal>().Die("msm8255 gpio bus: an input listener names gpio %u, past the %u pins", pin,
                              kPinCount);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    input_watches_.push_back({pin, std::move(listener)});
}

Msm8255GpioWindow* Msm8255GpioBus::OwnerLocked(uint32_t pin) const {
    for (Msm8255GpioWindow* window : windows_) {
        if (window->OwnsGpioPin(pin)) return window;
    }
    return nullptr;
}

void Msm8255GpioBus::ApplyModemPinConfig(uint32_t pin, uint32_t mux_config,
                                         bool output) {
    Msm8255GpioWindow* owner = nullptr;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        owner = OwnerLocked(pin);
    }
    if (owner == nullptr) {
        emu_.Get<Fatal>().Die(
            "msm8255 gpio bus: the modem configured gpio %u, which no window "
            "serves", pin);
    }
    owner->ApplyModemPinConfig(pin, mux_config, output);
}

void Msm8255GpioBus::RedriveOwnedPins(Msm8255GpioWindow* window) {
    std::lock_guard<std::mutex> lk(mtx_);
    RedriveOwnedPinsLocked(window);
}

void Msm8255GpioBus::AddOutputListener(uint32_t pin, OutputListener listener) {
    if (pin >= kPinCount) {
        emu_.Get<Fatal>().Die("msm8255 gpio bus: an output listener names gpio %u, past the %u pins", pin,
                              kPinCount);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    output_watches_.push_back({pin, Msm8255GpioOutput::kFloating, std::move(listener)});
}

void Msm8255GpioBus::PublishOutputs(uint32_t first_pin, uint32_t span, uint32_t enables, uint32_t levels) {
    std::vector<std::pair<const OutputListener*, Msm8255GpioOutput>> changed;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (OutputWatch& watch : output_watches_) {
            if (watch.pin < first_pin || watch.pin - first_pin >= span) continue;
            const uint32_t          bit = 1u << (watch.pin - first_pin);
            const Msm8255GpioOutput now = (enables & bit) == 0u ? Msm8255GpioOutput::kFloating
                                          : (levels & bit) != 0u ? Msm8255GpioOutput::kHigh
                                                                 : Msm8255GpioOutput::kLow;
            if (now == watch.last) continue;
            watch.last = now;
            changed.emplace_back(&watch.listener, now);
        }
    }
    for (const auto& [listener, level] : changed) (*listener)(level);
}

void Msm8255GpioBus::RedriveOwnedPinsLocked(Msm8255GpioWindow* window) {
    for (uint32_t pin = 0; pin < kPinCount; ++pin) {
        const uint8_t level = driven_[pin].load(std::memory_order_acquire);
        if (level == kUndriven) continue;
        if (!window->OwnsGpioPin(pin)) continue;
        window->SetGpioInputPin(pin, level == kDrivenHigh);
    }
}

REGISTER_SERVICE(Msm8255GpioBus);

}
