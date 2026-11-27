#pragma once

#include "atmel_mxt224e_map.h"
#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class AtmelMxt224eConfigChecks : public Service {
public:
    using Service::Service;
    using Config = std::array<uint8_t, kMxtNvmBytes>;

    bool ShouldRegister() override;

    void RequireModeled(const Config& config) const;
    bool ConfigError(const Config& config, uint8_t active_mode) const;
};

}
