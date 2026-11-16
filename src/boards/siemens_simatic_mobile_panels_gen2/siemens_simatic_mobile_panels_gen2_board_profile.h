#pragma once

#include <string_view>
#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"

namespace cerfcore {

struct DbBoard;

struct SiemensSimaticMobilePanelsGen2BoardProfile {
    std::string_view board;
    SiemensSimaticMobilePanelsGen2OpType op_type;
    SiemensSimaticMobilePanelsGen2PanelTiming timing;
    const char* touch_size_suffix;
    bool has_f_module;
};

const SiemensSimaticMobilePanelsGen2BoardProfile* TrySiemensSimaticMobilePanelsGen2BoardProfileFor(std::string_view board);
const SiemensSimaticMobilePanelsGen2BoardProfile& SiemensSimaticMobilePanelsGen2BoardProfileFor(std::string_view board);
SiemensSimaticMobilePanelsGen2Panel ResolveSiemensSimaticMobilePanelsGen2Panel(const DbBoard* device, const SiemensSimaticMobilePanelsGen2BoardProfile& profile);

}
