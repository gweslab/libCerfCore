#include "siemens_simatic_mobile_panels_gen2_board_profile.h"

#include <cerfcore/core/board_database.h>
#include "hmi_ktp400f_mobile_id.h"
#include "hmi_ktp700_mobile_id.h"
#include "hmi_ktp700f_arctic_mobile_id.h"
#include "hmi_ktp700f_hw_mobile_id.h"
#include "hmi_ktp700f_mobile_id.h"
#include "hmi_ktp900_mobile_id.h"
#include "hmi_ktp900f_mobile_id.h"
#include "hmi_tp1000f_mobile_id.h"
#include "hmi_tp1000f_ro_mobile_id.h"

namespace cerfcore {

namespace {

constexpr SiemensSimaticMobilePanelsGen2PanelTiming kPanelPh480272t006              = {1u, 42u, 8u, 10u, 2u, 4u, 9000000u, 24u};
constexpr SiemensSimaticMobilePanelsGen2PanelTiming kPanelLq070y3lw01               = {96u, 128u, 32u, 2u, 32u, 11u, 33260000u, 24u};
constexpr SiemensSimaticMobilePanelsGen2PanelTiming kPanelNl8048bc2409dBlankingStub = {64u, 128u, 32u, 2u, 32u, 11u, 32256000u, 24u};
constexpr SiemensSimaticMobilePanelsGen2PanelTiming kPanelG101evn013BlankingStub    = {16u, 56u, 56u, 4u, 6u, 6u, 68930000u, 24u};

constexpr SiemensSimaticMobilePanelsGen2BoardProfile kProfiles[] = {
    {BoardId::HmiKtp400fMobile,       SiemensSimaticMobilePanelsGen2OpType::Ktp400F,       kPanelPh480272t006,  "4in",   true},
    {BoardId::HmiKtp700Mobile,        SiemensSimaticMobilePanelsGen2OpType::Ktp700,        kPanelLq070y3lw01,   "7_9in", false},
    {BoardId::HmiKtp700fMobile,       SiemensSimaticMobilePanelsGen2OpType::Ktp700F,       kPanelLq070y3lw01,   "7_9in", true},
    {BoardId::HmiKtp900Mobile,        SiemensSimaticMobilePanelsGen2OpType::Ktp900,        kPanelNl8048bc2409dBlankingStub, "7_9in", false},
    {BoardId::HmiKtp900fMobile,       SiemensSimaticMobilePanelsGen2OpType::Ktp900F,       kPanelNl8048bc2409dBlankingStub, "7_9in", true},
    {BoardId::HmiTp1000fMobile,       SiemensSimaticMobilePanelsGen2OpType::Tp1000F,       kPanelG101evn013BlankingStub,    "10in",  true},
    {BoardId::HmiTp1000fRoMobile,     SiemensSimaticMobilePanelsGen2OpType::Tp1000FRo,     kPanelG101evn013BlankingStub,    "10in",  true},
    {BoardId::HmiKtp700fHwMobile,     SiemensSimaticMobilePanelsGen2OpType::Ktp700FHw,     kPanelLq070y3lw01,   "7_9in", true},
    {BoardId::HmiKtp700fArcticMobile, SiemensSimaticMobilePanelsGen2OpType::Ktp700FArctic, kPanelLq070y3lw01,   "7_9in", true},
};

}

const SiemensSimaticMobilePanelsGen2BoardProfile* TrySiemensSimaticMobilePanelsGen2BoardProfileFor(std::string_view board) {
    for (const auto& profile : kProfiles)
        if (profile.board == board) return &profile;
    return nullptr;
}

const SiemensSimaticMobilePanelsGen2BoardProfile& SiemensSimaticMobilePanelsGen2BoardProfileFor(std::string_view board) {
    return *TrySiemensSimaticMobilePanelsGen2BoardProfileFor(board);
}

SiemensSimaticMobilePanelsGen2Panel ResolveSiemensSimaticMobilePanelsGen2Panel(const DbBoard* dev, const SiemensSimaticMobilePanelsGen2BoardProfile& profile) {
    return {dev->lcd_panel_size->width, dev->lcd_panel_size->height, profile.timing};
}

}
