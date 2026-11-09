#include "../../socs/vr41xx/vr41xx_piu_panel.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "../../socs/vr41xx/vr41xx_piu_regs.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/boards/board_context.h>
#include "compaq_aero_1500_battery.h"
#include "compaq_aero_1500_family_id.h"

#include <cstdint>
#include <optional>

namespace cerfcore {

namespace {

using cerf_vr41xx_piu_detail::kAdcmdAdin0;
using cerf_vr41xx_piu_detail::kAdcmdAdin1;
using cerf_vr41xx_piu_detail::kAdcmdAdin2;

constexpr int      kAdin0CurrentSelectGpio = 35;
constexpr uint16_t kAdin1Reference         = 387u;
constexpr uint16_t kBackupCellWord         = 0x3A0u;

class CompaqAero1500TouchPanel : public Vr41xxPiuPanel {
public:
    using Vr41xxPiuPanel::Vr41xxPiuPanel;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    void OnReady() override {
        giu_     = &emu_.Get<Vr41xxGiu>();
        battery_ = &emu_.Get<CompaqAero1500Battery>();
    }

    std::optional<uint16_t> ConvertCommandPort(uint16_t adcmd, uint16_t pos_x,
                                               uint16_t pos_y) override {
        switch (adcmd) {
            case kAdcmdAdin0:
                return giu_->OutputDrive(kAdin0CurrentSelectGpio) == Vr41xxPinDrive::kHigh
                           ? battery_->ChargeCurrentWord()
                           : battery_->CellVoltageWord();
            case kAdcmdAdin1:
                return kAdin1Reference;
            case kAdcmdAdin2:
                return kBackupCellWord;
            default:
                emu_.Get<Fatal>().Die("CompaqAero1500TouchPanel: PIUCMDREG ADCMD=0x%X command "
                                      "scan (pen %u,%u)", adcmd, pos_x, pos_y);
        }
    }

    std::optional<uint16_t> PressureSample() override { return std::nullopt; }

    std::optional<uint16_t> AdPortScanSample(uint16_t port) override {
        emu_.Get<Fatal>().Die("CompaqAero1500TouchPanel: ADPortScan A/D port 0x%X", port);
    }

private:
    Vr41xxGiu*             giu_     = nullptr;
    CompaqAero1500Battery* battery_ = nullptr;
};

}

REGISTER_SERVICE_AS(CompaqAero1500TouchPanel, Vr41xxPiuPanel);

}
