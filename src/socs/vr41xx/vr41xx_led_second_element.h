#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/widget_icon.h>

#include <string>

namespace cerfcore {

class Vr41xxLedSecondElement : public Service {
public:
    using Service::Service;

    virtual std::string PackageName() const = 0;
    virtual std::string ElementName() const = 0;
    virtual WidgetRgb   LitColor() const = 0;
    virtual bool        Lit() const = 0;
};

}
