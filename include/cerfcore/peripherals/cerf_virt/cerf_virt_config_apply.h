#pragma once

#include "../../core/service.h"
#include "../../state/guest_reset.h"

#include <cstdint>

namespace cerfcore {

class CerfVirtConfigApply : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Apply(GuestReset reset, uint32_t prev_w, uint32_t prev_h);
};

}
