#include "adreno_a2xx_shader_decoder.h"
#include "adreno_a2xx_socs.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <algorithm>
#include <bit>

namespace cerfcore {

namespace {

constexpr uint64_t kAllRegisters = ~uint64_t{0};

bool ExecutesClause(uint32_t op) { return (op >= 1u && op <= 6u) || op == 13u || op == 14u; }

const uint32_t* Find(const std::unordered_map<uint32_t, uint32_t>& registers, uint32_t index) {
    const auto it = registers.find(index);
    return it == registers.end() ? nullptr : &it->second;
}

}

REGISTER_SERVICE(AdrenoA2xxShaderDecoder);

bool AdrenoA2xxShaderDecoder::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

AdrenoA2xxAluSource AdrenoA2xxShaderDecoder::Source(const std::array<uint32_t, 3>& w, uint32_t which,
                                                    bool force_constant, bool pixel,
                                                    const std::unordered_map<uint32_t, uint32_t>& registers) {
    AdrenoA2xxAluSource s;
    const uint32_t shift = (3u - which) * 8u;
    s.index     = static_cast<uint8_t>((w[2] >> shift) & 255u);
    s.swizzle   = static_cast<uint8_t>((w[1] >> shift) & 255u);
    s.negate    = ((w[1] >> (27u - which)) & 1u) != 0;
    s.temporary = !force_constant && ((w[2] >> (32u - which)) & 1u) != 0;
    if (s.temporary) {
        s.relative = (s.index & 64u) != 0;
        s.absolute = (s.index & 128u) != 0;
        return s;
    }
    const bool first = which == 1u || (which == 2u ? (w[2] & 0x80000000u) != 0
                                                   : (w[2] & 0xC0000000u) == 0xC0000000u);
    s.relative    = ((w[1] >> (first ? 31u : 30u)) & 1u) != 0;
    s.use_address = (w[1] & 0x20000000u) != 0;
    s.export32    = (w[0] & 0x803Fu) == 0x8020u && s.use_address && !s.relative;
    if (s.relative) return s;
    uint32_t base = 0;
    if (!s.export32) {
        const uint32_t* stored = Find(registers, pixel ? 0x2308u : 0x2307u);
        if (!stored) return s;
        base = *stored & 511u;
    }
    const uint32_t effective = base + s.index;
    if (effective >= 512u) return s;
    AdrenoA2xxVec4 raw{};
    for (uint32_t i = 0; i < 4u; ++i) {
        const uint32_t* stored = Find(registers, 0x4000u + effective * 4u + i);
        if (!stored) return s;
        raw[i] = std::bit_cast<float>(*stored);
    }
    for (uint32_t i = 0; i < 4u; ++i)
        s.value[i] = raw[((s.swizzle >> (i * 2u)) + i) & 3u] * (s.negate ? -1.0f : 1.0f);
    s.resolved = true;
    return s;
}

void AdrenoA2xxShaderDecoder::NoteClause(AdrenoA2xxShaderProgram& program, uint64_t cf,
                                         const std::unordered_map<uint32_t, uint32_t>& registers) {
    const uint32_t address  = static_cast<uint32_t>(cf & 511u);
    const uint32_t count    = static_cast<uint32_t>((cf >> 12) & 7u);
    const uint32_t sequence = static_cast<uint32_t>((cf >> 16) & 4095u);
    for (uint32_t k = 0; k < count; ++k) {
        const uint32_t slot = address + k;
        auto&          alu  = program.alu[slot];
        const auto&    w    = alu.words;
        if ((sequence >> (k * 2u)) & 1u) {
            program.read_registers |= ((w[0] >> 11) & 1u) ? kAllRegisters : uint64_t{1} << ((w[0] >> 5) & 63u);
            const uint32_t texture = (w[0] >> 20) & 31u;
            alu.texture_resolved = true;
            for (uint32_t i = 0; i < alu.texture_state.size(); ++i) {
                const uint32_t* stored = Find(registers, 0x4800u + texture * 6u + i);
                if (!stored) {
                    alu.texture_resolved = false;
                    break;
                }
                alu.texture_state[i] = *stored;
            }
            if (!alu.texture_resolved || (w[0] & 31u) == 0u) program.unresolved_fetches = true;
            program.fetches.push_back(static_cast<uint16_t>(slot));
            continue;
        }
        for (const auto& s : alu.sources) {
            if (!s.temporary && !s.resolved) program.unresolved_constants = true;
            if (!s.temporary) continue;
            program.read_registers |= s.relative ? kAllRegisters : uint64_t{1} << (s.index & 63u);
        }
        if (!alu.scalar_source.temporary && !alu.scalar_source.resolved) program.unresolved_constants = true;
        const uint32_t scalar_op = w[0] >> 26;
        if (scalar_op >= 42u && scalar_op <= 47u) {
            const uint32_t swizzle = w[1] & 255u;
            const uint32_t index   = (scalar_op & 1u) | (((w[2] >> 29) & 1u) << 1) | (swizzle & 60u);
            program.read_registers |= uint64_t{1} << index;
        }
        if (w[0] & 0x8000u) {
            program.written_exports |= uint64_t{1} << (w[0] & 63u);
            program.written_exports |= uint64_t{1} << ((w[0] >> 8) & 63u);
        }
    }
}

bool AdrenoA2xxShaderDecoder::StraightAlu(const AdrenoA2xxAluInstruction& alu) {
    const auto&    w         = alu.words;
    const uint32_t vector_op = (w[2] >> 24) & 31u, scalar_op = w[0] >> 26;
    if (((w[1] >> 27) & 2u) != 0u) return false;
    if (vector_op >= 20u && vector_op <= 29u) return false;
    if (scalar_op >= 23u && scalar_op <= 24u) return false;
    if (scalar_op >= 27u && scalar_op <= 39u) return false;
    if ((w[0] & 64u) != 0u || (w[0] & 0x4000u) != 0u) return false;
    for (const auto& s : alu.sources) if (s.relative) return false;
    return !alu.scalar_source.relative;
}

bool AdrenoA2xxShaderDecoder::StraightFetch(const std::array<uint32_t, 3>& w) {
    if ((w[1] >> 31) != 0u) return false;
    return ((w[0] >> 11) & 1u) == 0u && ((w[0] >> 18) & 1u) == 0u;
}

void AdrenoA2xxShaderDecoder::NoteStraight(AdrenoA2xxShaderProgram& program) {
    std::vector<uint16_t> steps;
    for (uint32_t pc = 0; pc < program.limit; ++pc) {
        const uint64_t cf = program.control[pc];
        const uint32_t op = static_cast<uint32_t>(cf >> 44);
        if (op == 0u || op == 12u || op == 15u) continue;
        if (op != 1u && op != 2u) return;
        const uint32_t address  = static_cast<uint32_t>(cf & 511u);
        const uint32_t count    = static_cast<uint32_t>((cf >> 12) & 7u);
        const uint32_t sequence = static_cast<uint32_t>((cf >> 16) & 4095u);
        for (uint32_t k = 0; k < count; ++k) {
            const uint32_t slot  = address + k;
            const bool     fetch = ((sequence >> (k * 2u)) & 1u) != 0u;
            if (fetch ? !StraightFetch(program.alu[slot].words) : !StraightAlu(program.alu[slot])) return;
            steps.push_back(static_cast<uint16_t>(slot | (fetch ? kAdrenoA2xxStraightFetch : 0u)));
        }
        if (op == 2u) {
            program.straight       = true;
            program.straight_steps = std::move(steps);
            return;
        }
    }
}

AdrenoA2xxShaderProgram AdrenoA2xxShaderDecoder::Decode(std::span<const uint32_t> words, bool pixel,
                                                        const std::unordered_map<uint32_t, uint32_t>& registers) {
    AdrenoA2xxShaderProgram program;
    program.words = words;
    program.pixel = pixel;
    if (words.empty() || words.size() % 3u || words.size() > 1536u) {
        program.reject       = "program length";
        program.reject_value = static_cast<uint32_t>(words.size());
        return program;
    }
    const uint32_t entries = static_cast<uint32_t>(words.size() / 3u * 2u);
    program.control.resize(entries);
    for (uint32_t pc = 0; pc < entries; ++pc) {
        const size_t offset = size_t(pc / 2u) * 3u;
        program.control[pc] = (pc & 1u) ? (uint64_t(words[offset + 1u]) >> 16) | (uint64_t(words[offset + 2u]) << 16)
                                        : uint64_t(words[offset]) | (uint64_t(words[offset + 1u] & 0xFFFFu) << 32);
    }
    uint32_t limit = entries;
    for (uint32_t i = 0; i < limit; ++i) {
        const uint64_t cf = program.control[i];
        if (!ExecutesClause(static_cast<uint32_t>(cf >> 44))) continue;
        const uint32_t address = static_cast<uint32_t>(cf & 511u);
        const uint32_t count   = static_cast<uint32_t>((cf >> 12) & 7u);
        if (!count) continue;
        if (uint64_t(address + count) * 3u > words.size()) {
            program.reject       = "instruction span";
            program.reject_value = address;
            return program;
        }
        if (address * 2u <= i) {
            program.reject       = "instruction overlaps control";
            program.reject_value = address;
            return program;
        }
        limit = std::min(limit, address * 2u);
    }
    program.limit = limit;
    program.alu.resize(words.size() / 3u);
    for (size_t slot = 0; slot < program.alu.size(); ++slot) {
        auto& alu = program.alu[slot];
        alu.words = {words[slot * 3u], words[slot * 3u + 1u], words[slot * 3u + 2u]};
        for (uint32_t which = 1; which <= 3u; ++which)
            alu.sources[which - 1u] = Source(alu.words, which, false, pixel, registers);
        const uint32_t scalar_op = alu.words[0] >> 26;
        alu.scalar_source = Source(alu.words, 3u, scalar_op >= 42u && scalar_op <= 47u, pixel, registers);
    }
    for (uint32_t i = 0; i < limit; ++i) {
        const uint64_t cf = program.control[i];
        if (ExecutesClause(static_cast<uint32_t>(cf >> 44))) NoteClause(program, cf, registers);
    }
    NoteStraight(program);
    return program;
}

}
