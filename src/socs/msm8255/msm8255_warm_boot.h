#pragma once

#include <cerfcore/core/service.h>

namespace cerfcore {

class Msm8255WarmBoot : public Service {
public:
    using Service::Service;

    virtual void ResumeCollapsedCore() = 0;
};

}
