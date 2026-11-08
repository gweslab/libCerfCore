#include <cerfcore/core/service.h>

#include <cerfcore/boards/board_context.h>
#include "pangolin_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget_registry.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/pcmcia/pcmcia_auto_insert.h"
#include "../../peripherals/pcmcia/pcmcia_slot.h"
#include "../../peripherals/pcmcia/pcmcia_space_router.h"
#include "../../socs/sa11xx/sa11xx_gpio.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kGpioCd  = 6u;
constexpr uint32_t kGpioIrq = 5u;

class PangolinPcmcia : public Service, public PcmciaSlotHost {
public:
    explicit PangolinPcmcia(CerfEmulator& emu)
        : Service(emu), slot_(emu, *this, "PC Card slot") {}

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Pangolin;
    }

    void OnReady() override {
        emu_.Get<PcmciaSpaceRouter>().ProvideSockets(&slot_, nullptr);
        emu_.Get<HostWidgetRegistry>().Register(&slot_);

        auto& gpio = emu_.Get<Sa11xxGpio>();
        gpio.DriveInputPin(kGpioCd, true);    /* empty: CF_CD idles high (no card) */
        gpio.DriveInputPin(kGpioIrq, true);   /* READY/nIREQ deasserted (idle high) */

        emu_.Get<PcmciaAutoInsert>().InsertDefaultNetworkCard(slot_);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) slot_.PowerCycleCard();
        });
    }

    void OnShutdown() override { slot_.OnShutdown(); }

    void OnCardDetectChanged(PcmciaSlot& slot) override {
        const bool present = slot.HasCard();
        slot.SetPowered(present);
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioCd, !present);
    }
    void OnReadyIreqLow(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, false);
    }
    void OnReadyIreqHigh(PcmciaSlot&) override {
        emu_.Get<Sa11xxGpio>().DriveInputPin(kGpioIrq, true);
    }
    uint8_t UndrivenDataLane() const override { return kUnstatedUndrivenDataLane; }

private:
    PcmciaSlot slot_;
};

}  /* namespace */

REGISTER_SERVICE(PangolinPcmcia);

}
