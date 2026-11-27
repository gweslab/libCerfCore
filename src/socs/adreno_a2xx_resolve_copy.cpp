#include "adreno_a2xx_resolve_copy.h"
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace {

constexpr uint32_t kSampleSelectMask = 0x7u;
constexpr uint32_t kControlFieldMask = 0xFFu;

}

AdrenoA2xxResolveCopy::AdrenoA2xxResolveCopy(uint32_t control, uint32_t dest_base, uint32_t dest_pitch,
                                             uint32_t dest_info, uint32_t dest_offset, uint32_t source_info,
                                             uint32_t depth_info, uint32_t depth_clear)
    : control_(control), dest_base_(dest_base), dest_pitch_(dest_pitch), dest_info_(dest_info),
      dest_offset_(dest_offset), source_info_(source_info), depth_info_(depth_info), depth_clear_(depth_clear) {}

AdrenoA2xxResolveCopy::Fault AdrenoA2xxResolveCopy::Check() const {
    const uint32_t clear_mask = (control_ >> kClearMaskShift) & kClearMaskAllBytes;
    if ((control_ & ~kControlFieldMask) != 0u) return {"resolve copy control reserved", control_};
    if ((control_ & kSampleSelectMask) != 0u) return {"resolve sample select", control_};
    if (!ClearsDepth() && clear_mask != 0u) return {"resolve clear mask without depth clear", control_};
    if (ClearsDepth() && AdrenoA2xxDepthStencil::InfoReserved(depth_info_))
        return {"resolve depth clear format", depth_info_};
    if (ClearsDepth() && DepthBytes() == 2u && clear_mask != kClearMaskAllBytes)
        return {"resolve 16-bit depth clear mask", control_};
    if (ClearsDepth() && clear_mask == 0u) return {"resolve depth clear without a byte mask", control_};
    if (dest_info_ & 7u) return {"resolve destination endian", dest_info_};
    if (!(dest_info_ & 8u)) return {"resolve tiled destination", dest_info_};
    if (((dest_info_ >> 4) & 15u) != (source_info_ & 15u)) return {"resolve format conversion", dest_info_};
    if (((dest_info_ >> 8) & 3u) > 1u) return {"resolve destination swap", dest_info_};
    if (((source_info_ >> 9) & 3u) > 1u) return {"resolve source swap", source_info_};
    if (dest_info_ & 0xFFFFFC00u & ~0x3C000u) return {"resolve destination dither/reserved", dest_info_};
    if (dest_offset_ & 0xFC000000u) return {"resolve offset", dest_offset_};
    if ((dest_base_ & 4095u) != 0u || dest_pitch_ > 511u || dest_pitch_ == 0u)
        return {"resolve destination", dest_base_};
    return {nullptr, 0u};
}

const char* AdrenoA2xxResolveCopy::Unsupported() const { return Check().reason; }

uint32_t AdrenoA2xxResolveCopy::UnsupportedValue() const { return Check().value; }

std::array<uint8_t, 4> AdrenoA2xxResolveCopy::ClearedDepth(const uint8_t* old_pixel) const {
    std::array<uint8_t, 4> pixel{};
    if (DepthBytes() == 2u) {
        cerf::le::Put16(pixel.data(), static_cast<uint16_t>(depth_clear_ >> 16));
        return pixel;
    }
    const uint32_t mask = (control_ >> kClearMaskShift) & kClearMaskAllBytes;
    for (uint32_t i = 0; i < pixel.size(); ++i)
        pixel[i] = (mask & (1u << i)) != 0u ? static_cast<uint8_t>(depth_clear_ >> (8u * i)) : old_pixel[i];
    return pixel;
}

}
