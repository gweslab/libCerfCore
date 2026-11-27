#include "../../peripherals/battery_pack.h"
#include "../../socs/msm8255/msm8255_usb_port.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nokia_sea_ray_family_id.h"

#include <utility>

namespace cerfcore {

namespace {

class NokiaSeaRayUsbPort : public Msm8255UsbPort {
public:
    using Msm8255UsbPort::Msm8255UsbPort;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override { battery_ = &emu_.Get<BatteryPack>(); }

    Msm8255UsbAttachment Attachment() const override {
        return battery_->ExternalPower() ? Msm8255UsbAttachment::kDedicatedCharger : Msm8255UsbAttachment::kNone;
    }

    void AddChangeListener(std::function<void()> fn) override { battery_->AddChangeListener(std::move(fn)); }

private:
    BatteryPack* battery_ = nullptr;
};

}

REGISTER_SERVICE_AS(NokiaSeaRayUsbPort, Msm8255UsbPort);

}
