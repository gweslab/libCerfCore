#include "../../peripherals/hitachi_hd64461/hd64461_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/lcd/lcd_pixel_expand.h>
#include "../../peripherals/hitachi_hd64461/hd64461_gpio.h"
#include "../../peripherals/hitachi_hd64461/hd64461_gpio_wiring.h"
#include "hp_600_family_id.h"

#include <atomic>

namespace cerfcore {

namespace {

constexpr uint8_t kLcdPowerPin = 0x01u;

class Hp600Hd64461Panel : public Hd64461Panel {
public:
    using Hd64461Panel::Hd64461Panel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
    }

    void OnReady() override {
        Hd64461Gpio& gpio = emu_.Get<Hd64461Gpio>();
        wiring_           = &emu_.Get<Hd64461GpioWiring>();
        powered_.store(PowerFromPin(), std::memory_order_release);
        gpio.AddPinListener([this] { UpdatePower(); });
    }

    Kind PanelKind() const override { return Kind::kColorStn8Bit; }

    uint32_t PresentedWidth(uint32_t dots, bool monochrome_dots) const override {
        return monochrome_dots ? lcd_pixel::SubPixelPanelWidth(dots) : dots;
    }

    bool Powered() const override { return powered_.load(std::memory_order_acquire); }

    void SetPowerListener(std::function<void()> listener) override { listener_ = std::move(listener); }

private:
    bool PowerFromPin() const { return (wiring_->NetLevels(Hd64461Gpio::kPortB) & kLcdPowerPin) == 0u; }

    void UpdatePower() {
        const bool on = PowerFromPin();
        if (powered_.exchange(on, std::memory_order_acq_rel) == on) return;
        LOG(Lcd, "Hp600Hd64461Panel: LCD power %s\n", on ? "on" : "off");
        listener_();
    }

    const Hd64461GpioWiring* wiring_ = nullptr;
    std::atomic<bool>        powered_{false};
    std::function<void()> listener_;
};

}

REGISTER_SERVICE_AS(Hp600Hd64461Panel, Hd64461Panel);

}
