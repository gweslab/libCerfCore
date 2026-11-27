#pragma once
#include "adreno_a2xx_depth_stencil.h"
#include <array>
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxResolveCopy {
public:
    AdrenoA2xxResolveCopy(uint32_t control, uint32_t dest_base, uint32_t dest_pitch, uint32_t dest_info,
                          uint32_t dest_offset, uint32_t source_info, uint32_t depth_info, uint32_t depth_clear);

    const char* Unsupported() const;
    uint32_t    UnsupportedValue() const;

    uint32_t DestBase() const { return dest_base_; }
    uint32_t DestPitch() const { return dest_pitch_ * 32u; }
    uint32_t DestColorInfo() const { return (dest_info_ & 0x300u) << 1; }
    uint32_t ColorMask() const { return (dest_info_ >> 14) & 15u; }
    uint32_t OffsetX() const { return dest_offset_ & 8191u; }
    uint32_t OffsetY() const { return (dest_offset_ >> 13) & 8191u; }

    static bool ControlClearsDepth(uint32_t control) { return (control & kDepthClearEnable) != 0u; }
    static bool ControlClears(uint32_t control) {
        return (control & (kDepthClearEnable | (kClearMaskAllBytes << kClearMaskShift))) != 0u;
    }
    bool     ClearsDepth() const { return ControlClearsDepth(control_); }
    uint32_t DepthBase() const { return AdrenoA2xxDepthStencil::InfoBase(depth_info_); }
    uint32_t DepthBytes() const { return AdrenoA2xxDepthStencil::InfoBytes(depth_info_); }
    std::array<uint8_t, 4> ClearedDepth(const uint8_t* old_pixel) const;

private:
    static constexpr uint32_t kDepthClearEnable  = 0x8u;
    static constexpr uint32_t kClearMaskShift    = 4u;
    static constexpr uint32_t kClearMaskAllBytes = 0xFu;

    struct Fault {
        const char* reason;
        uint32_t    value;
    };
    Fault Check() const;

    uint32_t control_;
    uint32_t dest_base_;
    uint32_t dest_pitch_;
    uint32_t dest_info_;
    uint32_t dest_offset_;
    uint32_t source_info_;
    uint32_t depth_info_;
    uint32_t depth_clear_;
};

}
