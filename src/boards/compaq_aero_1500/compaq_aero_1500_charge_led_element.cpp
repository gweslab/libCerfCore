#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../socs/vr41xx/vr41xx_led_second_element.h"
#include "compaq_aero_1500_family_id.h"

#include <string>

namespace cerfcore {

namespace {

constexpr int       kChargeLedGpio = 39;
constexpr WidgetRgb kClrChargeLit{255, 200, 40};

class CompaqAero1500ChargeLedElement : public Vr41xxLedSecondElement {
public:
    using Vr41xxLedSecondElement::Vr41xxLedSecondElement;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    void OnReady() override { giu_ = &emu_.Get<Vr41xxGiu>(); }

    std::string PackageName() const override { return "Charger/alarm LED"; }
    std::string ElementName() const override { return "Charge LED"; }
    WidgetRgb   LitColor() const override { return kClrChargeLit; }
    bool        Lit() const override { return giu_->OutputDrive(kChargeLedGpio) == PinDrive::kHigh; }

private:
    Vr41xxGiu* giu_ = nullptr;
};

}

REGISTER_SERVICE_AS(CompaqAero1500ChargeLedElement, Vr41xxLedSecondElement);

}
