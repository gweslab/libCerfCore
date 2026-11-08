#pragma once

#include "../core/service.h"
#include "pointer_source.h"

#include <cstdint>

namespace cerfcore {

/* Relative pointing device (laptop touchpad / captured mouse), fed by the
   host mouse-lock path while HostInputCapture is captured. */

constexpr uint32_t kRelMouseLeft  = 0x1u;
constexpr uint32_t kRelMouseRight = 0x2u;

class RelativeMouseInput : public Service, public PointerSource {
public:
    using Service::Service;
    ~RelativeMouseInput() override = default;

    virtual void OnRelativeMove(int dx, int dy, uint32_t button_mask) = 0;

    /* PointerSource - concretes override SourceName per controller. */
    std::string    SourceName()       const override { return "Stock mouse"; }
    const char*    IconResourceName() const override { return "cursor"; }
    int            SourcePriority()   const override { return 0; }
    PointerKind    Kind()             const override { return PointerKind::Relative; }
};

}
