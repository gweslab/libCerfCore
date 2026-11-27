#include "adreno_a2xx_blend.h"
#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kFactorZero                  = 0u;
constexpr uint32_t kFactorOne                   = 1u;
constexpr uint32_t kFactorSrcColor              = 4u;
constexpr uint32_t kFactorOneMinusSrcColor      = 5u;
constexpr uint32_t kFactorSrcAlpha              = 6u;
constexpr uint32_t kFactorOneMinusSrcAlpha      = 7u;
constexpr uint32_t kFactorDstColor              = 8u;
constexpr uint32_t kFactorOneMinusDstColor      = 9u;
constexpr uint32_t kFactorDstAlpha              = 10u;
constexpr uint32_t kFactorOneMinusDstAlpha      = 11u;
constexpr uint32_t kFactorConstantColor         = 12u;
constexpr uint32_t kFactorOneMinusConstantColor = 13u;
constexpr uint32_t kFactorConstantAlpha         = 14u;
constexpr uint32_t kFactorOneMinusConstantAlpha = 15u;
constexpr uint32_t kFactorSrcAlphaSaturate      = 16u;

constexpr uint32_t kCombAdd             = 0u;
constexpr uint32_t kCombSubtract        = 1u;
constexpr uint32_t kCombMin             = 2u;
constexpr uint32_t kCombMax             = 3u;
constexpr uint32_t kCombReverseSubtract = 4u;

constexpr uint32_t kReservedBits = 0xE000E000u;
constexpr unsigned kAlpha        = 3u;
constexpr unsigned kSourceShift  = 0u;
constexpr unsigned kCombShift    = 5u;
constexpr unsigned kDestShift    = 8u;

bool KnownFactor(uint32_t factor) {
    return factor == kFactorZero || factor == kFactorOne ||
           (factor >= kFactorSrcColor && factor <= kFactorOneMinusConstantAlpha);
}

bool ConstantFactor(uint32_t factor) {
    return factor >= kFactorConstantColor && factor <= kFactorOneMinusConstantAlpha;
}

}

AdrenoA2xxBlendEquation::AdrenoA2xxBlendEquation(uint32_t control) : control_(control) {}

uint32_t AdrenoA2xxBlendEquation::Field(unsigned channel, unsigned shift, uint32_t mask) const {
    return (control_ >> ((channel == kAlpha ? 16u : 0u) + shift)) & mask;
}

const char* AdrenoA2xxBlendEquation::Unsupported() const {
    if ((control_ & kReservedBits) != 0u) return "blend force/reserved bits";
    for (unsigned channel : {0u, kAlpha}) {
        const uint32_t source = Field(channel, kSourceShift, 31u), destination = Field(channel, kDestShift, 31u);
        if (!KnownFactor(source) && !(channel != kAlpha && source == kFactorSrcAlphaSaturate))
            return "blend source factor";
        if (!KnownFactor(destination)) return "blend destination factor";
        if (Field(channel, kCombShift, 7u) > kCombReverseSubtract) return "blend combine function";
    }
    return nullptr;
}

bool AdrenoA2xxBlendEquation::UsesConstant() const {
    for (unsigned channel : {0u, kAlpha})
        if (ConstantFactor(Field(channel, kSourceShift, 31u)) || ConstantFactor(Field(channel, kDestShift, 31u)))
            return true;
    return false;
}

float AdrenoA2xxBlendEquation::Factor(uint32_t factor, unsigned channel, const AdrenoA2xxVec4& source,
                                      const AdrenoA2xxVec4& destination, const AdrenoA2xxVec4& constant) const {
    switch (factor) {
        case kFactorZero:                  return 0.0f;
        case kFactorOne:                   return 1.0f;
        case kFactorSrcColor:              return source[channel];
        case kFactorOneMinusSrcColor:      return 1.0f - source[channel];
        case kFactorSrcAlpha:              return source[kAlpha];
        case kFactorOneMinusSrcAlpha:      return 1.0f - source[kAlpha];
        case kFactorDstColor:              return destination[channel];
        case kFactorOneMinusDstColor:      return 1.0f - destination[channel];
        case kFactorDstAlpha:              return destination[kAlpha];
        case kFactorOneMinusDstAlpha:      return 1.0f - destination[kAlpha];
        case kFactorConstantColor:         return constant[channel];
        case kFactorOneMinusConstantColor: return 1.0f - constant[channel];
        case kFactorConstantAlpha:         return constant[kAlpha];
        case kFactorOneMinusConstantAlpha: return 1.0f - constant[kAlpha];
        case kFactorSrcAlphaSaturate:      return std::min(source[kAlpha], 1.0f - destination[kAlpha]);
    }
    return 0.0f;
}

AdrenoA2xxVec4 AdrenoA2xxBlendEquation::Apply(const AdrenoA2xxVec4& source, const AdrenoA2xxVec4& destination,
                                              const AdrenoA2xxVec4& constant) const {
    AdrenoA2xxVec4 result{};
    for (unsigned channel = 0; channel < 4u; ++channel) {
        const float weighted_source =
            source[channel] * Factor(Field(channel, kSourceShift, 31u), channel, source, destination, constant);
        const float weighted_destination =
            destination[channel] * Factor(Field(channel, kDestShift, 31u), channel, source, destination, constant);
        float value = 0.0f;
        switch (Field(channel, kCombShift, 7u)) {
            case kCombAdd:             value = weighted_source + weighted_destination; break;
            case kCombSubtract:        value = weighted_source - weighted_destination; break;
            case kCombMin:             value = std::min(source[channel], destination[channel]); break;
            case kCombMax:             value = std::max(source[channel], destination[channel]); break;
            case kCombReverseSubtract: value = weighted_destination - weighted_source; break;
        }
        result[channel] = std::clamp(value, 0.0f, 1.0f);
    }
    return result;
}

}
