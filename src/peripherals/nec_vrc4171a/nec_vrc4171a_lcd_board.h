#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

struct NecVrc4171aPanelInterface {
    uint16_t panel_select  = 0u;
    uint16_t panel_signals = 0u;
    uint16_t vertical_half = 0u;
};

class NecVrc4171aLcdBoard : public Service {
public:
    using Service::Service;

    virtual uint32_t FrameMemoryBase() const = 0;
    virtual uint32_t FrameMemoryBytes() const = 0;
    virtual uint32_t RegisterBase() const = 0;
    virtual NecVrc4171aPanelInterface PanelInterface() const = 0;
};

}
