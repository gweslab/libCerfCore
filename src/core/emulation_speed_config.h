#pragma once

#include <cerfcore/core/emulation_multiplier.h>
#include <cerfcore/core/service.h>

#include <nlohmann/json_fwd.hpp>

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
    void       Load(const std::string& path, CfgBrokenFile broken);
    Multiplier ReadMultiplier(const nlohmann::json& root, const char* key, bool zero_allowed,
                              const std::string& path, Multiplier current);

    Multiplier throttle_;
    Multiplier cpu_rate_;
};

}
