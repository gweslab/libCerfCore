#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class IsaDebugPortConfig : public Service {
public:
    using Service::Service;

    virtual uint32_t IsaWindowBase() const = 0;
};

}
