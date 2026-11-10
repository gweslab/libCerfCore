#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class Hd64461Panel : public Service {
public:
    enum class Kind : uint8_t {
        kColorStn8Bit,
    };

    using Service::Service;

    virtual Kind     PanelKind() const                                          = 0;
    virtual uint32_t PresentedWidth(uint32_t dots, bool monochrome_dots) const = 0;
    virtual bool     Powered() const                                            = 0;
    virtual void     SetPowerListener(std::function<void()> listener)           = 0;
};

}
