#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class ShProcessorConfig : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool HasDsp() const { return dsp_; }

private:
    bool dsp_ = false;
};

}
