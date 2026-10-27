#pragma once

#include "../core/service.h"

namespace cerfcore {

class CustomizationsEditor : public Service {
public:
    using Service::Service;

    virtual bool Open(bool force_reboot) = 0;
};

}
