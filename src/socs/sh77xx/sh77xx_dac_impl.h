#pragma once

#include <cstdint>

#include "../reg_window_impl.h"

namespace cerfcore {

namespace sh77xx_dac {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kBase  = 0x040000A0u;
inline constexpr uint32_t kDadr0 = 0u;
inline constexpr uint32_t kDadr1 = 2u;
inline constexpr uint32_t kDacr  = 4u;

constexpr RegWindowModel WindowOf(OtherReset other_reset) {
    RegWindowModel w{kBase, 5u, 5u, false, {}, 1u};
    w.reg[kDadr0] = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xFFu, 0x00u, 0x00u, other_reset};
    w.reg[kDadr1] = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xFFu, 0x00u, 0x00u, other_reset};
    w.reg[kDacr]  = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xE0u, 0x1Fu, 0x00u, other_reset};
    return w;
}

}

}
