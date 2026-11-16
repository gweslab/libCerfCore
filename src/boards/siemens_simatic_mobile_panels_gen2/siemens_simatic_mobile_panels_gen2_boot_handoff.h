#pragma once

#include <cerfcore/core/service.h>
#include "siemens_simatic_mobile_panels_gen2_hardware_info.h"

#include <cstdint>

namespace cerfcore {

struct SiemensSimaticMobilePanelsGen2OalLayout {
    const char* log_tag;
    SiemensSimaticMobilePanelsGen2OpType op_type;
    SiemensSimaticMobilePanelsGen2Panel panel;
};

class SiemensSimaticMobilePanelsGen2BootHandoff : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Place(const SiemensSimaticMobilePanelsGen2OalLayout& oal);
};

}
