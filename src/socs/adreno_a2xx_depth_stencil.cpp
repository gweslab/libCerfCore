#include "adreno_a2xx_depth_stencil.h"
#include <cerfcore/core/byte_order.h>
#include <bit>

namespace cerfcore {

namespace {

constexpr uint32_t kZFuncShift = 4u;
constexpr uint32_t kFrontShift = 8u;
constexpr uint32_t kBackShift  = 20u;
constexpr uint32_t kFieldMask  = 0x7u;

constexpr uint32_t kStencilMax = 0xFFu;

constexpr uint32_t kDepth24Max         = 0xFFFFFFu;
constexpr double   kDepth24Scale       = 16777215.0;
constexpr float    kDepth24LowRange    = 0.03125f;
constexpr double   kDepth24LowRounding = 0.5;
constexpr double   kDepth24Rounding    = 0.5625;
constexpr double   kDepth16Rounding    = 0.500029981136322;
constexpr uint32_t kDepth16Shift       = 8u;

uint32_t Depth16(double depth) {
    const float z = static_cast<float>(depth);
    if (z <= 0.0f) return 0u;
    if (z >= 1.0f) return kDepth24Max >> kDepth16Shift;
    return static_cast<uint32_t>(double(z) * kDepth24Scale + kDepth16Rounding) >> kDepth16Shift;
}

uint32_t Depth24(double depth) {
    float z = static_cast<float>(depth);
    if (z <= 0.0f) return 0u;
    if (z >= 1.0f) return kDepth24Max;
    if (z < kDepth24LowRange) {
        z = std::bit_cast<float>(std::bit_cast<uint32_t>(z) + 1u);
        return static_cast<uint32_t>(double(z) * kDepth24Scale + kDepth24LowRounding);
    }
    return static_cast<uint32_t>(double(z) * kDepth24Scale + kDepth24Rounding);
}

bool Compare(uint32_t func, uint32_t incoming, uint32_t stored) {
    switch (func) {
    case 0u: return false;
    case 1u: return incoming < stored;
    case 2u: return incoming == stored;
    case 3u: return incoming <= stored;
    case 4u: return incoming > stored;
    case 5u: return incoming != stored;
    case 6u: return incoming >= stored;
    default: return true;
    }
}

uint32_t StencilOp(uint32_t op, uint32_t value, uint32_t ref) {
    switch (op) {
    case 0u: return value;
    case 1u: return 0u;
    case 2u: return ref;
    case 3u: return value == kStencilMax ? value : value + 1u;
    case 4u: return value == 0u ? value : value - 1u;
    case 5u: return ~value & kStencilMax;
    case 6u: return (value + 1u) & kStencilMax;
    default: return (value - 1u) & kStencilMax;
    }
}

}

AdrenoA2xxDepthStencil::AdrenoA2xxDepthStencil(uint32_t control, uint32_t info, uint32_t front_ref_mask,
                                               uint32_t back_ref_mask)
    : control_(control), info_(info), front_ref_mask_(front_ref_mask), back_ref_mask_(back_ref_mask),
      depth_test_((control & kZEnable) != 0u),
      depth_write_(depth_test_ && (control & kZWriteEnable) != 0u),
      stencil_test_((control & kStencilEnable) != 0u) {}

bool AdrenoA2xxDepthStencil::MayWrite() const { return depth_write_ || stencil_test_; }

const char* AdrenoA2xxDepthStencil::Unsupported() const {
    if (!Attached()) return nullptr;
    if (InfoReserved(info_)) return "depth attachment format bits";
    return nullptr;
}

AdrenoA2xxDepthStencil::Face AdrenoA2xxDepthStencil::FaceFor(bool front) const {
    const bool     back     = !front && (control_ & kBackfaceEnable) != 0u;
    const uint32_t shift    = back ? kBackShift : kFrontShift;
    const uint32_t ref_mask = back ? back_ref_mask_ : front_ref_mask_;
    return { (control_ >> shift) & kFieldMask,         (control_ >> (shift + 3u)) & kFieldMask,
             (control_ >> (shift + 6u)) & kFieldMask,  (control_ >> (shift + 9u)) & kFieldMask,
             ref_mask & kStencilMax, (ref_mask >> 8) & kStencilMax, (ref_mask >> 16) & kStencilMax };
}

AdrenoA2xxDepthStencil::Outcome AdrenoA2xxDepthStencil::Test(double depth, bool front, const uint8_t* stored,
                                                             std::array<uint8_t, 4>& updated) const {
    const bool     wide     = Bytes() == 4u;
    const uint32_t incoming = wide ? Depth24(depth) : Depth16(depth);

    const uint32_t word           = wide ? cerf::le::U32(stored) : cerf::le::U16(stored);
    const uint32_t stored_depth   = wide ? word >> 8 : word;
    const uint32_t stored_stencil = wide ? word & kStencilMax : 0u;

    const bool depth_passes =
        !depth_test_ || Compare((control_ >> kZFuncShift) & kFieldMask, incoming, stored_depth);
    bool     passes  = depth_passes;
    uint32_t stencil = stored_stencil;
    if (stencil_test_ && wide) {
        const Face     face           = FaceFor(front);
        const bool     stencil_passes = Compare(face.func, face.ref & face.mask, stored_stencil & face.mask);
        const uint32_t op             = !stencil_passes ? face.fail : depth_passes ? face.zpass : face.zfail;
        stencil = (stored_stencil & ~face.write_mask) | (StencilOp(op, stored_stencil, face.ref) & face.write_mask);
        passes  = stencil_passes && depth_passes;
    }

    const uint32_t new_depth = passes && depth_write_ ? incoming : stored_depth;
    const uint32_t new_word  = wide ? (new_depth << 8) | stencil : new_depth;
    if (wide) cerf::le::Put32(updated.data(), new_word);
    else      cerf::le::Put16(updated.data(), static_cast<uint16_t>(new_word));
    return { passes, new_word != word };
}

}
