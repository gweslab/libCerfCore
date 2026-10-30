#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class SamsungSmdk2410FnKey : public Service {
public:
    using Service::Service;

    virtual uint8_t Scancode() const = 0;
    virtual bool    HasApp1Key() const = 0;
};

}
