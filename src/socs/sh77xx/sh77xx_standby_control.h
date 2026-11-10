#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

template <uint8_t kRegister>
class Sh77xxStandbyControl : public Peripheral {
public:
    using Peripheral::Peripheral;

    using ChangeListener = std::function<void(uint8_t from, uint8_t to)>;

    virtual bool ModuleStopped(uint8_t mstp) const                   = 0;
    virtual void RegisterChangeListener(ChangeListener listener) = 0;
};

using Sh77xxStbcr  = Sh77xxStandbyControl<1u>;
using Sh77xxStbcr2 = Sh77xxStandbyControl<2u>;

}
