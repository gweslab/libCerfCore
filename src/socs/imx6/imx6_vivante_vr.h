#pragma once

#include <cstdint>

#include "imx6_vivante_mem.h"
#include "imx6_vivante_surface_access.h"

namespace cerfcore {

namespace imx6_vivante {

class VivanteVr : protected VivanteSurfaceAccess {
public:
    VivanteVr(VivanteMem& mem) : VivanteSurfaceAccess(mem) {}

    void Execute(uint32_t start_value);

};

}

}
