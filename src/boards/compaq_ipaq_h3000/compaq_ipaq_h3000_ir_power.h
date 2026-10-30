#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class CompaqIpaqH3000IrPower : public Service {
public:
    using Service::Service;

    virtual bool Powered() const = 0;
};

}
