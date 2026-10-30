#pragma once

#include "service.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class UserConfigWriter : public Service {
public:
    using Service::Service;

    void WriteConfigurableScreenSize(uint32_t width, uint32_t height);
    void WriteHud(const std::string& hud);
};

}
