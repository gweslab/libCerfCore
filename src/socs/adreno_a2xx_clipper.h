#pragma once
#include "adreno_a2xx_shader.h"
#include <array>
#include <vector>

namespace cerfcore {

class AdrenoA2xxClipper {
public:
    AdrenoA2xxClipper(bool directx_z, bool window_linear_varyings);

    bool Outside(const AdrenoA2xxVec4& position) const;
    std::vector<AdrenoA2xxShaderState> Clip(const std::array<AdrenoA2xxShaderState, 3>& vertices) const;

private:
    double Distance(const AdrenoA2xxShaderState& vertex, unsigned plane) const;
    bool   ZeroPlane(unsigned plane) const;

    bool directx_z_;
    bool window_linear_varyings_;
};

}
