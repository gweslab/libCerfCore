#pragma once

#include "../core/service.h"
#include "widget_control.h"

#include <vector>

namespace cerfcore {

class DeviceButtons : public Service {
public:
    using Service::Service;
    ~DeviceButtons() override = default;

    using ButtonGroup = std::vector<ActionControl>;

    virtual std::vector<ButtonGroup> ButtonGroups() = 0;
};

}
