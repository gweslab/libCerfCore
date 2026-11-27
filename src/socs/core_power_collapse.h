#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class CorePowerCollapse : public Service {
public:
    using Service::Service;

    virtual bool ResumeIfCollapsed() = 0;
};

}
