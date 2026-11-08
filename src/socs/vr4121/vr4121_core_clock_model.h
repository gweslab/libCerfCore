#pragma once

#include "../vr41xx/vr41xx_core_clock_impl.h"

namespace cerfcore {

inline constexpr Vr41xxCoreClockModel kVr4121CoreClock{
    64u,
    4u,
    {{15u, 12u, 0x060Du}, {13u, 12u, 0x060Du}, {12u, 12u, 0x060Du}, {10u, 16u, 0x041Du},
     {9u, 16u, 0x041Du},  {8u, 20u, 0x0439u},  {7u, 24u, 0x0079u},  {0u, 0u, 0x0000u}},
    {kVr41xxEcReserved, 7u, 1u, 3u, 4u, 5u, 6u, kVr41xxEcReserved},
};

}
