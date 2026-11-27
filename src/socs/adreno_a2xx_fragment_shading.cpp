#include "adreno_a2xx_fragment_shading.h"

#include "adreno_a2xx_float.h"
#include "adreno_a2xx_shader_sequencer.h"
#include "adreno_a2xx_socs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

#include <bit>
#include <cmath>

namespace cerfcore {

REGISTER_SERVICE(AdrenoA2xxFragmentShading);

bool AdrenoA2xxFragmentShading::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxFragmentShading::ShadeQuad(const std::array<AdrenoA2xxVaryings, 3>& vertices,
                                          const LaneWeights& weights,
                                          const std::array<double, 3>& inverse_w, bool perspective,
                                          const std::array<bool, 4>& covered,
                                          const AdrenoA2xxShaderProgram& pixel_program,
                                          const std::unordered_map<uint32_t, uint32_t>& registers,
                                          const AdrenoA2xxMmu& mmu_config, AdrenoA2xxPageCache* texels,
                                          std::array<AdrenoA2xxShaderState, 4>& fragments) {
    LaneWeights corrected = weights;
    bool quad_valid = true;
    if (perspective) {
        for (auto& lane : corrected) {
            double total = 0;
            for (unsigned i = 0; i < 3u; ++i) {
                lane[i] *= inverse_w[i];
                total += lane[i];
            }
            if (!AdrenoA2xxFinite(total) || total == 0) quad_valid = false;
            for (auto& weight : lane) weight /= total;
        }
    }
    const uint64_t varyings = vertices[0].mask & vertices[1].mask & vertices[2].mask;
    auto interpolate = [&](unsigned lane) {
        for (uint64_t rest = varyings; rest != 0u; rest &= rest - 1u) {
            const unsigned slot = static_cast<unsigned>(std::countr_zero(rest));
            for (unsigned component = 0; component < 4u; ++component) {
                float value = 0;
                for (unsigned i = 0; i < 3u; ++i)
                    value += static_cast<float>(corrected[lane][i] * vertices[i].values[slot][component]);
                fragments[lane].registers[slot][component] = value;
            }
        }
    };
    auto& sequencer = emu_.Get<AdrenoA2xxShaderSequencer>();
    if (quad_valid) {
        for (unsigned lane = 0; lane < 4u; ++lane) interpolate(lane);
        sequencer.RunQuad(pixel_program, registers, mmu_config, texels, fragments);
        return;
    }
    for (unsigned lane = 0; lane < 4u; ++lane) {
        if (!covered[lane]) continue;
        interpolate(lane);
        sequencer.Run(pixel_program, registers, mmu_config, texels, fragments[lane]);
    }
}

}
