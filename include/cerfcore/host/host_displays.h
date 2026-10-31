#pragma once

#include "../core/service.h"

namespace cerfcore {

class HostDisplays : public Service {
public:
    using Service::Service;

    virtual int HighestRefreshRate() = 0;
};

}
