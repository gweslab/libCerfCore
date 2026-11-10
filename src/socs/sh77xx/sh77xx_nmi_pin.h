#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Sh77xxNmiPin : public Service {
public:
    using Service::Service;

    virtual bool High() const = 0;
};

}
