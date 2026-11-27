#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

enum class Msm8255UsbAttachment : uint8_t {
    kNone,
    kDedicatedCharger,
};

class Msm8255UsbPort : public Service {
public:
    using Service::Service;

    virtual Msm8255UsbAttachment Attachment() const                     = 0;
    virtual void                 AddChangeListener(std::function<void()> fn) = 0;
};

}
