#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/host/machine_object_tree.h>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_f_module_device.h"
#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"
#include "siemens_simatic_mobile_panels_gen2_key_led.h"

#include <cstdint>
#include <mutex>

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2Gpio5 final : public Imx6GpioInputSource, public SiemensSimaticMobilePanelsGen2KeyLedOwner {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && TrySiemensSimaticMobilePanelsGen2BoardProfileFor(board->GetBoardId()) != nullptr;
    }

    void OnReady() override {
        const SiemensSimaticMobilePanelsGen2BoardProfile& profile = SiemensSimaticMobilePanelsGen2BoardProfileFor(emu_.Get<BoardContext>().GetBoardId());
        module_ = emu_.TryGet<SiemensSimaticMobilePanelsGen2FModuleDevice>();
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
        if (module_) module_->AddLinesListener(&SiemensSimaticMobilePanelsGen2Gpio5::OnLinesChanged, this);
        has_leds_ = profile.op_type != SiemensSimaticMobilePanelsGen2OpType::Tp1000F && profile.op_type != SiemensSimaticMobilePanelsGen2OpType::Tp1000FRo;
        led8_mask_ = profile.op_type == SiemensSimaticMobilePanelsGen2OpType::Ktp700FHw ? (1u << 16u) : (1u << 4u);
        if (has_leds_) {
            auto& tree = emu_.Get<MachineObjectTree>();
            tree.Add(&led8_);
            tree.Add(&led9_);
        }
        Reevaluate();
    }

    uint32_t GpioBase() const override { return 0x020AC000u; }

    uint32_t ApplyPadInputs(uint32_t inputs) override {
        if (!module_) return inputs;
        return module_->Lines().ready ? (inputs | kReadyMask) : (inputs & ~kReadyMask);
    }

    uint32_t DrivenPins() const override { return module_ && module_->Lines().ready_driven ? kReadyMask : 0u; }

    void OnEffectiveOutputs(uint32_t dr, uint32_t gdir) override {
        if (module_) module_->ObservePanelAcknowledge((gdir & kAckMask) != 0u, (dr & kAckMask) != 0u);
        if (!has_leds_) return;
        {
            std::lock_guard<std::mutex> lk(outputs_mutex_);
            dr_ = dr;
            gdir_ = gdir;
        }
        led8_.Refresh();
        led9_.Refresh();
    }

    bool KeyLedLit(uint32_t line) const override {
        const uint32_t mask = line == 8u ? led8_mask_ : kLed9Mask;
        std::lock_guard<std::mutex> lk(outputs_mutex_);
        return (gdir_ & mask) != 0u && (dr_ & mask) != 0u;
    }

    void SaveState(StateWriter& w) override {
        if (module_) module_->SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        if (module_) module_->RestoreState(r);
    }
    void PostRestore() override {
        if (module_) module_->PostRestore();
        Reevaluate();
    }

private:
    static constexpr uint32_t kReadyMask = 1u << 5u;
    static constexpr uint32_t kAckMask = 1u << 6u;
    static constexpr uint32_t kLed9Mask = 1u << 17u;

    static void OnLinesChanged(void* context) { static_cast<SiemensSimaticMobilePanelsGen2Gpio5*>(context)->Reevaluate(); }

    SiemensSimaticMobilePanelsGen2FModuleDevice* module_ = nullptr;
    bool has_leds_ = false;
    uint32_t led8_mask_ = 0u;
    static constexpr ObjectRgb kPushbuttonLightColor{255, 255, 255};

    mutable std::mutex outputs_mutex_;
    uint32_t dr_ = 0u;
    uint32_t gdir_ = 0u;
    SiemensSimaticMobilePanelsGen2KeyLed led8_{*this, 8u, 8u, kPushbuttonLightColor};
    SiemensSimaticMobilePanelsGen2KeyLed led9_{*this, 9u, 9u, kPushbuttonLightColor};
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2Gpio5);

}
