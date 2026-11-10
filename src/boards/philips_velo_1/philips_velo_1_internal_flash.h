#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <span>

namespace cerfcore {

class PhilipsVelo1InternalFlash : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t                 ResetMirrorPa() const { return mirror_pa_; }
    std::span<const uint8_t> ResetMirrorBytes() const { return mirror_; }

private:
    uint32_t                 mirror_pa_ = 0;
    std::span<const uint8_t> mirror_;
};

}
