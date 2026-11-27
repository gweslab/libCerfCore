#pragma once
#include "adreno_a2xx_shader_program.h"
#include <cerfcore/core/service.h>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace cerfcore {

class AdrenoA2xxShaderDecoder : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    AdrenoA2xxShaderProgram Decode(std::span<const uint32_t> words, bool pixel,
                                   const std::unordered_map<uint32_t, uint32_t>& registers);

private:
    static AdrenoA2xxAluSource Source(const std::array<uint32_t, 3>& words, uint32_t which, bool force_constant,
                                      bool pixel, const std::unordered_map<uint32_t, uint32_t>& registers);
    static void NoteClause(AdrenoA2xxShaderProgram& program, uint64_t cf,
                           const std::unordered_map<uint32_t, uint32_t>& registers);
    static bool StraightAlu(const AdrenoA2xxAluInstruction& alu);
    static bool StraightFetch(const std::array<uint32_t, 3>& words);
    static void NoteStraight(AdrenoA2xxShaderProgram& program);
};

}
