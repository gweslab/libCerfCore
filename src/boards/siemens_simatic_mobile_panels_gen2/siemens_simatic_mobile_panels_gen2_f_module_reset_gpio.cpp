#include "siemens_simatic_mobile_panels_gen2_f_module_device.h"
#include "../../socs/imx6/imx6_gpio_pads.h"
#include "../../socs/imx6/imx6_iomuxc.h"
#include "../../socs/imx6/imx6_variant.h"
#include <cerfcore/core/fatal.h>

#include <cerfcore/boards/board_context.h>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"

#include <cstdint>

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2FModuleResetGpio final : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        if (!board) return false;
        const auto* profile = TrySiemensSimaticMobilePanelsGen2BoardProfileFor(board->GetBoardId());
        return profile && profile->has_f_module;
    }

    void OnReady() override {
        module_ = &emu_.Get<SiemensSimaticMobilePanelsGen2FModuleDevice>();
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
    }
    uint32_t GpioBase() const override { return 0x020A8000u; }

    void OnEffectiveOutputs(uint32_t dr, uint32_t gdir) override {
        const Imx6GpioPad* pad = FindImx6GpioPad(Imx6GpioPadsFor(emu_.Get<Imx6Variant>().Family()), 4u, 27u);
        const auto& iomuxc = emu_.Get<Imx6Iomuxc>();
        const uint32_t mux = iomuxc.PadRegister(pad->mux_offset) & kImx6PadMuxMode;
        if (mux != pad->gpio_mode)
            emu_.Get<Fatal>().Die("F-module NRST: GPIO4_IO27 pad mux 0x%X", mux);
        bool high = (dr & kResetMask) != 0u;
        if ((gdir & kResetMask) == 0u) {
            const uint32_t ctl = iomuxc.PadRegister(pad->ctl_offset);
            const Imx6PadInputLevel input = ResolveImx6PadInputLevel(ctl, line_high_, level_known_);
            if (input.known && !input.high)
                emu_.Get<Fatal>().Die("F-module NRST: pad pull-down or low keeper against RPU (0x%08X)", ctl);
            high = true;
        }
        if (!level_known_ || high != line_high_) {
            level_known_ = true;
            line_high_ = high;
            module_->ObserveModuleReset(high);
        }
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint8_t>("level_known", level_known_ ? 1u : 0u);
        w.Write<uint8_t>("line_high", line_high_ ? 1u : 0u);
    }
    void RestoreState(StateReader& r) override {
        uint8_t known = 0;
        uint8_t high = 0;
        r.Read("level_known", known);
        r.Read("line_high", high);
        level_known_ = known != 0u;
        line_high_ = high != 0u;
    }
    void OnControllerReset() override {
        level_known_ = false;
        line_high_ = true;
    }

private:
    static constexpr uint32_t kResetMask = 1u << 27u;

    SiemensSimaticMobilePanelsGen2FModuleDevice* module_ = nullptr;
    bool level_known_ = false;
    bool line_high_ = true;
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2FModuleResetGpio);

}
