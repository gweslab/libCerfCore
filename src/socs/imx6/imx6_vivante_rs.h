#pragma once

#include <cstdint>

#include "imx6_vivante_mem.h"
#include "imx6_vivante_surface_access.h"

namespace cerfcore {

namespace imx6_vivante {

class VivanteRs : protected VivanteSurfaceAccess {
public:
    VivanteRs(VivanteMem& mem) : VivanteSurfaceAccess(mem) {}

    void ExecuteRsInPlace(uint32_t tile_count);
    void ExecuteRs();

};

}

}
