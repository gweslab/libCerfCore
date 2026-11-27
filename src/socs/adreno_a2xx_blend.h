#pragma once
#include "adreno_a2xx_shader.h"
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxBlendEquation {
public:
    explicit AdrenoA2xxBlendEquation(uint32_t control);

    const char*    Unsupported() const;
    bool           UsesConstant() const;
    AdrenoA2xxVec4 Apply(const AdrenoA2xxVec4& source, const AdrenoA2xxVec4& destination,
                         const AdrenoA2xxVec4& constant) const;

private:
    uint32_t Field(unsigned channel, unsigned shift, uint32_t mask) const;
    float    Factor(uint32_t factor, unsigned channel, const AdrenoA2xxVec4& source,
                    const AdrenoA2xxVec4& destination, const AdrenoA2xxVec4& constant) const;

    uint32_t control_;
};

}
