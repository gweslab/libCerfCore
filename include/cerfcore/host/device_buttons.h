#pragma once

#include "../core/service.h"
#include "widget_control.h"

#include <vector>

namespace cerfcore {

class DeviceButtons : public Service {
public:
    using Service::Service;
    ~DeviceButtons() override = default;

    virtual std::vector<ActionControl> Actions() = 0;
};

}
