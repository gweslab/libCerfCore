#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class CompaqIpaqH3000BatteryCurve : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint16_t VoltageFor(int fill_percent) const;

private:
    const std::array<uint16_t, 10>* tenth_voltages_ = nullptr;
};

}
