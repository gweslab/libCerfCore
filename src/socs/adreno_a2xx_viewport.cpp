#include "adreno_a2xx_viewport.h"

namespace cerfcore {

namespace {

constexpr uint32_t kDefinedBits       = 0xF3Fu;
constexpr uint32_t kXyTransformBits   = 0xFu;
constexpr uint32_t kTransformBits     = 0x3Fu;
constexpr uint32_t kXyFormat          = 0x100u;
constexpr uint32_t kZFormat           = 0x200u;
constexpr uint32_t kW0Format          = 0x400u;

}

AdrenoA2xxViewportTransform::AdrenoA2xxViewportTransform(uint32_t value) : value_(value) {}

uint32_t AdrenoA2xxViewportTransform::Value() const { return value_; }

bool AdrenoA2xxViewportTransform::Valid() const {
    const uint32_t xy = value_ & kXyTransformBits;
    return (value_ & ~kDefinedBits) == 0u && (xy == 0u || xy == kXyTransformBits);
}

bool AdrenoA2xxViewportTransform::WindowSpace() const { return (value_ & kXyTransformBits) == 0u; }

bool AdrenoA2xxViewportTransform::Untransformed() const { return (value_ & kTransformBits) == 0u; }

bool AdrenoA2xxViewportTransform::Scale(unsigned axis) const {
    return ((value_ >> (axis * 2u)) & 1u) != 0u;
}

bool AdrenoA2xxViewportTransform::Offset(unsigned axis) const {
    return ((value_ >> (axis * 2u + 1u)) & 1u) != 0u;
}

bool AdrenoA2xxViewportTransform::XyPremultiplied() const { return (value_ & kXyFormat) != 0u; }

bool AdrenoA2xxViewportTransform::ZPremultiplied() const { return (value_ & kZFormat) != 0u; }

bool AdrenoA2xxViewportTransform::TakesReciprocalW() const { return (value_ & kW0Format) != 0u; }

double AdrenoA2xxViewportTransform::InverseW(float w) const { return TakesReciprocalW() ? 1.0 / w : w; }

}
