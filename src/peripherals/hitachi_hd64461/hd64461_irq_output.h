#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Hd64461IrqOutput : public Service {
public:
    using Service::Service;

    virtual void SetIrq0Asserted(bool asserted) = 0;
};

}
