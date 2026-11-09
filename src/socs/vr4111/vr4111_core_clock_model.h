#pragma once

#include "../vr41xx/vr41xx_core_clock_impl.h"

namespace cerfcore {

inline constexpr Vr41xxCoreClockModel kVr4111CoreClock{
    64u,
    4u,
    {{24u, 8u, 0u}, {19u, 12u, 0u}, {18u, 12u, 0u}, {17u, 12u, 0u}, {15u, 12u, 0u}, {14u, 12u, 0u},
     {13u, 16u, 0u}, {12u, 16u, 0u}},
    {kVr41xxEcReserved, kVr41xxEcReserved, 0u, 1u, 2u, kVr41xxEcReserved, kVr41xxEcReserved,
     kVr41xxEcReserved},
};

}
