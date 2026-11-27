#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class NokiaSeaRayHwRevisionStraps : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t Revision() const;
};

}
