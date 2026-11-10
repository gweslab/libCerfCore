#pragma once

#include "../reg_window_impl.h"

#include <cstdint>

namespace cerfcore {

namespace sh77xx_ccr {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kCcrPa = 0x1FFFFFECu;

constexpr RegWindowModel Window(uint16_t writable, uint16_t halt_on_set, OtherReset other_reset) {
    RegWindowModel w{kCcrPa, 4u, 2u, true, {}, 2u, true};
    w.reg[0] = RegSpec{ReadKind::kStored, WriteKind::kStored, writable, 0u, halt_on_set, other_reset};
    w.reg[1] = RegSpec{ReadKind::kStored, WriteKind::kStored, 0u, 0u, 0xFFFFu, other_reset};
    return w;
}

}

}
