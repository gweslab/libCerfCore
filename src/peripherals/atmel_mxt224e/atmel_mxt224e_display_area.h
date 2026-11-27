#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class AtmelMxt224eDisplayArea : public Service {
public:
    using Service::Service;

    struct Area {
        uint16_t touch_width    = 0u;
        uint16_t touch_height   = 0u;
        uint16_t display_width  = 0u;
        uint16_t display_height = 0u;
    };

    virtual Area DisplayArea() const = 0;
};

}
