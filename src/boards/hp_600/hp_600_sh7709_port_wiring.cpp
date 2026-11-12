#include "../../socs/sh77xx/sh77xx_port_wiring.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_pfc.h"
#include "../../socs/sh77xx/sh77xx_port_data.h"
#include "hp_600_family_id.h"
#include "hp_600_net_hold.h"
#include "hp_600_sh7709_key_matrix.h"

namespace cerfcore {

namespace {

constexpr uint8_t kPortCKeySenseAndLid = 0xFFu;
constexpr uint8_t kPortDRowsAndPins    = 0xFBu;
constexpr uint8_t kPortERowsAndPins    = 0xFFu;
constexpr uint8_t kPortFKeySense       = 0xFFu;
constexpr uint8_t kPortGInputs         = 0xFFu;
constexpr uint8_t kPortHOnKey          = 0x01u;
constexpr uint8_t kPortHCompanionIrq   = 0x10u;
constexpr uint8_t kPortHWired          = kPortHOnKey | kPortHCompanionIrq;
constexpr uint8_t kPortScSerialCts     = 0x80u;

constexpr uint8_t kPortGLidOpen         = 0x01u;
constexpr uint8_t kPortGMainBatteryOut  = 0x04u;
constexpr uint8_t kPortGNotificationLed = 0x08u;
constexpr uint8_t kPortGBoardDriven     = kPortGLidOpen | kPortGMainBatteryOut | kPortGNotificationLed;
constexpr uint8_t kPortJNotificationLed = 0x02u;

class Hp600Sh7709PortWiring : public Sh77xxPortWiring {
public:
    using Sh77xxPortWiring::Sh77xxPortWiring;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override { matrix_ = &emu_.Get<Hp600Sh7709KeyMatrix>(); }

    void TrackDrive(const Sh77xxPortData& ports) override {
        nets_.Track(ports);
        matrix_->TrackRows(ports);
    }

    uint8_t WiredInputs(uint32_t port) const override {
        switch (port) {
            case kSh77xxPortC:  return kPortCKeySenseAndLid;
            case kSh77xxPortD:  return kPortDRowsAndPins;
            case kSh77xxPortE:  return kPortERowsAndPins;
            case kSh77xxPortF:  return kPortFKeySense;
            case kSh77xxPortG:  return kPortGInputs;
            case kSh77xxPortH:  return kPortHWired;
            case kSh77xxPortSc: return kPortScSerialCts;
            default:            return 0u;
        }
    }

    uint8_t NetLevels(uint32_t port) const override {
        const uint8_t nets = nets_.Levels(port);
        if (port != kSh77xxPortG) return nets;
        const bool led_net_high = (nets_.Levels(kSh77xxPortJ) & kPortJNotificationLed) != 0u;
        return static_cast<uint8_t>((nets & ~kPortGBoardDriven) | kPortGLidOpen |
                                    (led_net_high ? kPortGNotificationLed : 0u));
    }

    void SaveState(StateWriter& w) override { nets_.SaveState(w); }

    void RestoreState(StateReader& r) override { nets_.RestoreState(r); }

private:
    Hp600Sh7709KeyMatrix*                           matrix_ = nullptr;
    Hp600NetHold<kSh77xxPortCount, kSh77xxPortPins> nets_;
};

}

REGISTER_SERVICE_AS(Hp600Sh7709PortWiring, Sh77xxPortWiring);

}
