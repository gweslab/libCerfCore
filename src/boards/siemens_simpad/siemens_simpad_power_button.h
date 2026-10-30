#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kSimpadPowerButtonCode = 6;

class SiemensSimpadPowerButton : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetPressed(bool pressed);
};

}
