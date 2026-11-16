#include "../../socs/imx6/imx6_gpio_bus.h"
#include "../../socs/imx6/imx6_gpio_source.h"
#include "../../peripherals/ti_tsc2017/tsc2017_host_state.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

#include <cstdint>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

namespace cerfcore {

namespace {

class SiemensSimaticMobilePanelsGen2TouchGpio : public Imx6GpioInputSource {
public:
    using Imx6GpioInputSource::Imx6GpioInputSource;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }
    void OnReady() override {
        emu_.Get<Imx6GpioBus>().RegisterSource(this);
        emu_.Get<Tsc2017HostState>().SetIrqChangedCallback(&SiemensSimaticMobilePanelsGen2TouchGpio::OnPenChanged, this);
    }

    uint32_t GpioBase() const override { return 0x020B0000u; }

    uint32_t ApplyPadInputs(uint32_t inputs) override {
        if (emu_.Get<Tsc2017HostState>().PenIrqLineHigh())
            inputs |= kPenIrqMask;
        else
            inputs &= ~kPenIrqMask;
        return inputs;
    }
    uint32_t DrivenPins() const override { return kPenIrqMask; }
private:
    static constexpr uint32_t kPenIrqMask = 0x00008000u;

    static void OnPenChanged(void* ctx) { static_cast<SiemensSimaticMobilePanelsGen2TouchGpio*>(ctx)->Reevaluate(); }
};

}

REGISTER_SERVICE(SiemensSimaticMobilePanelsGen2TouchGpio);

}
