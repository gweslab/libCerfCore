#pragma once

#include <cerfcore/core/emulation_multiplier.h>
#include <cerfcore/core/service.h>

#include <cstdint>
#include <string>

namespace cerfcore {

enum class CfgBrokenFile : uint8_t;

class EmulationSpeedConfig : public Service {
public:
    using Multiplier = EmulationMultiplier;

    using Service::Service;

    void OnReady() override;

    Multiplier Throttle() const { return throttle_; }
    Multiplier CpuRate() const { return cpu_rate_; }

private:
    void Load(const std::string& path, CfgBrokenFile broken);

    Multiplier throttle_;
    Multiplier cpu_rate_;
};

}
