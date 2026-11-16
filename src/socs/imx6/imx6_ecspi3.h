#pragma once

#include "imx6_ecspi.h"

namespace cerfcore {

class Imx6Ecspi3 final : public Imx6Ecspi<0x02010000u> {
public:
    using Imx6Ecspi::Imx6Ecspi;
};

}
