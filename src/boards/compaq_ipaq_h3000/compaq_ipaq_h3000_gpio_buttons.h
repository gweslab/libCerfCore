#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kIpaqH3000PowerButtonPin  = 0;
inline constexpr uint32_t kIpaqH3000ActionButtonPin = 18;

class CompaqIpaqH3000GpioButtons : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetPressed(uint32_t pin, bool pressed);

private:
    void ReleaseAll();
};

}
