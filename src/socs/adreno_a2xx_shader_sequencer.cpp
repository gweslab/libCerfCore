#include "adreno_a2xx_shader_sequencer.h"
#include "adreno_a2xx_shader.h"
#include "adreno_a2xx_socs.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <algorithm>
#include <vector>

namespace cerfcore {

REGISTER_SERVICE(AdrenoA2xxShaderSequencer);

bool AdrenoA2xxShaderSequencer::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxShaderSequencer::OnReady() { shader_ = &emu_.Get<AdrenoA2xxShader>(); }

void AdrenoA2xxShaderSequencer::Run(const AdrenoA2xxShaderProgram& program,
                                    const std::unordered_map<uint32_t, uint32_t>& regs, const AdrenoA2xxMmu& config,
                                    AdrenoA2xxPageCache* texels, AdrenoA2xxShaderState& state) {
    RunInvocations(program, regs, config, texels, {&state, 1});
}

void AdrenoA2xxShaderSequencer::RunQuad(const AdrenoA2xxShaderProgram& program,
                                        const std::unordered_map<uint32_t, uint32_t>& regs,
                                        const AdrenoA2xxMmu& config, AdrenoA2xxPageCache* texels,
                                        std::array<AdrenoA2xxShaderState, 4>& states) {
    if (!program.straight) {
        RunInvocations(program, regs, config, texels, states);
        return;
    }
    AdrenoA2xxShader& shader = *shader_;
    for (auto& state : states) {
        state.export_mask = 0;
        state.memory_exports.clear();
        state.killed = false;
        state.address_register = 0;
        state.texture_lod = 0;
        state.texture_gradients_x = {};
        state.texture_gradients_y = {};
        state.loop_address = 0;
    }
    std::array<bool, 4>  predicate{};
    std::array<float, 4> previous{};
    for (const uint16_t step : program.straight_steps) {
        const auto& instruction = program.alu[step & ~kAdrenoA2xxStraightFetch];
        if ((step & kAdrenoA2xxStraightFetch) == 0u) {
            for (size_t lane = 0; lane < 4u; ++lane)
                shader.Alu(instruction, program.pixel, regs, states[lane], predicate[lane], previous[lane]);
            continue;
        }
        const uint32_t source = (instruction.words[0] >> 5) & 63u;
        for (auto& state : states) state.gradient_mask &= ~(uint64_t{1} << source);
        for (size_t lane = 0; lane < 4u; ++lane) {
            for (unsigned component = 0; component < 4u; ++component) {
                states[lane].gradients_x[source][component] =
                    states[lane | 1u].registers[source][component] - states[lane & ~size_t{1}].registers[source][component];
                states[lane].gradients_y[source][component] =
                    states[lane | 2u].registers[source][component] - states[lane & ~size_t{2}].registers[source][component];
            }
            states[lane].gradient_mask |= uint64_t{1} << source;
        }
        AdrenoA2xxShaderState* const lanes[] = {&states[0], &states[1], &states[2], &states[3]};
        shader.FetchLanes(instruction, regs, config, texels, lanes, predicate);
    }
}

void AdrenoA2xxShaderSequencer::RunInvocations(const AdrenoA2xxShaderProgram& program,
                                               const std::unordered_map<uint32_t, uint32_t>& regs,
                                               const AdrenoA2xxMmu& config, AdrenoA2xxPageCache* texels,
                                               std::span<AdrenoA2xxShaderState> states) {
    AdrenoA2xxShader& shader = *shader_;
    if (program.reject) shader.Reject(program.reject, program.reject_value);
    const bool     pixel = program.pixel;
    const uint32_t limit = program.limit;
    struct Loop { uint32_t remaining, id; int32_t step, saved_address; };
    struct Cursor {
        uint32_t pc = 0, steps = 0, address = 0, remaining = 0, sequence = 0;
        bool predicate = false, end = false, finished = false;
        float previous = 0;
        std::vector<uint32_t> return_stack;
        std::vector<Loop> loops;
    };
    std::array<Cursor, 4> cursors{};
    const bool quad = states.size() == 4;
    for (auto& state : states) {
        state.export_mask = 0;
        state.memory_exports.clear();
        state.killed = false;
        state.address_register = 0;
        state.texture_lod = 0;
        state.texture_gradients_x = {};
        state.texture_gradients_y = {};
        state.loop_address = 0;
    }
    auto finish = [&](size_t lane) {
        cursors[lane].finished = true;
        if (states[lane].killed) {
            states[lane].export_mask = 0;
            states[lane].memory_exports.clear();
        }
    };
    auto advance = [&](size_t lane) {
        auto& cursor = cursors[lane];
        auto& state = states[lane];
        auto& pc = cursor.pc;
        auto& predicate = cursor.predicate;
        auto& previous = cursor.previous;
        auto& loops = cursor.loops;
        auto& return_stack = cursor.return_stack;
        while (!cursor.finished) {
            if (cursor.remaining) {
                if (cursor.sequence & 1u) return true;
                const bool was_killed = state.killed;
                shader.Alu(program.alu[cursor.address], pixel, regs, state, predicate, previous);
                ++cursor.address;
                --cursor.remaining;
                cursor.sequence >>= 2;
                if (state.killed && !was_killed) {
                    if (!quad) finish(lane);
                    return false;
                }
                continue;
            }
            if (cursor.end) { finish(lane); return false; }
            if (cursor.steps++ == 4096u) shader.Reject("instruction budget", 4096u);
            if (pc >= limit) shader.Reject("control fallthrough", pc);
            const uint64_t cf = program.control[pc++];
            const uint32_t op = static_cast<uint32_t>(cf >> 44);
            if (op == 12u) {
                continue;
            }
            if (op == 15u) {
                continue;
            }
            if (op == 0u) continue;
            if (op == 7u || op == 8u) {
                if (cf & 0x7FFFFE0FC00ull) shader.Reject("loop reserved fields", static_cast<uint32_t>(cf));
                const uint32_t target = static_cast<uint32_t>(cf & 1023u);
                if (target >= limit) shader.Reject("loop target", target);
                const uint32_t id = static_cast<uint32_t>((cf >> 16) & 31u);
                if (op == 8u) {
                    if (loops.empty() || loops.back().id != id) shader.Reject("unmatched loop end", id);
                    auto& loop = loops.back();
                    if (--loop.remaining) {
                        state.loop_address += loop.step;
                        pc = target;
                    } else {
                        state.loop_address = loop.saved_address;
                        loops.pop_back();
                    }
                    continue;
                }
                const uint32_t value = shader.Register(regs, 0x4908u + id);
                const uint32_t count = value & 255u;
                if (!count) { pc = target; continue; }
                if (loops.size() == 64u) shader.Reject("loop stack budget", 64u);
                const uint32_t step = (value >> 16) & 255u;
                loops.push_back({count, id, step < 128u ? int32_t(step) : int32_t(step) - 256, state.loop_address});
                state.loop_address = static_cast<int32_t>((value >> 8) & 255u);
                continue;
            }
            if (op == 10u) {
                if (!return_stack.empty()) {
                    pc = return_stack.back();
                    return_stack.pop_back();
                }
                continue;
            }
            auto boolean = [&] {
                const uint32_t index = static_cast<uint32_t>((cf >> 34) & 255u);
                return ((shader.Register(regs, 0x4900u + index / 32u) >> (index % 32u)) & 1u) != 0;
            };
            const bool condition = ((cf >> 42) & 1u) != 0;
            if (op == 9u || op == 11u) {
                if ((cf >> 43) & 1u) shader.Reject("jump address mode", 1u);
                const bool force = ((cf >> 13) & 1u) != 0;
                const bool test = force ? condition : ((cf >> 14) & 1u) ? predicate : boolean();
                if (force || test == condition) {
                    const uint32_t target = static_cast<uint32_t>(cf & 1023u);
                    if (target >= limit) shader.Reject("jump target", target);
                    const bool forward = ((cf >> 33) & 1u) != 0;
                    if (op == 11u && forward != (target > pc - 1u)) shader.Reject("jump direction", target);
                    if (op == 9u) {
                        if (return_stack.size() == 64u) shader.Reject("call stack budget", 64u);
                        return_stack.push_back(pc);
                    }
                    pc = target;
                }
                continue;
            }
            if (!((op >= 1u && op <= 6u) || op == 13u || op == 14u)) shader.Reject("control opcode", op);
            bool execute = true;
            if (op == 3u || op == 4u || op == 13u || op == 14u) execute = boolean() == condition;
            if (op == 5u || op == 6u) execute = predicate == condition;
            if (execute) {
                cursor.address = static_cast<uint32_t>(cf & 511u);
                cursor.remaining = static_cast<uint32_t>((cf >> 12) & 7u);
                cursor.sequence = static_cast<uint32_t>((cf >> 16) & 4095u);
            }
            cursor.end = op == 2u || ((op == 4u || op == 6u || op == 14u) && execute);
            }
        return false;
    };
    for (;;) {
        std::array<bool, 4> ready{};
        bool pending_kill = false;
        for (size_t lane = 0; lane < states.size(); ++lane) {
            ready[lane] = advance(lane);
            pending_kill |= !ready[lane] && !cursors[lane].finished;
        }
        bool live = false;
        for (size_t lane = 0; lane < states.size(); ++lane)
            live |= !cursors[lane].finished && !states[lane].killed;
        if (!live) {
            for (size_t lane = 0; lane < states.size(); ++lane) finish(lane);
            return;
        }
        if (pending_kill) continue;
        if (std::none_of(ready.begin(), ready.end(), [](bool value) { return value; })) return;
        bool aligned = quad;
        for (size_t lane = 0; lane < states.size(); ++lane)
            aligned &= ready[lane] && cursors[lane].address == cursors[0].address;
        std::array<uint32_t, 4> sources{};
        std::array<bool, 4> source_valid{};
        for (size_t lane = 0; quad && lane < states.size(); ++lane) if (ready[lane]) {
            const auto& words = program.alu[cursors[lane].address].words;
            const uint32_t word = words[0];
            const bool active = !(words[1] >> 31) ||
                cursors[lane].predicate == ((words[2] >> 31) != 0);
            const int64_t index = int64_t((word >> 5) & 63u) +
                ((word & (1u << 11)) ? states[lane].loop_address : 0);
            source_valid[lane] = active && index >= 0 && index < 64;
            aligned &= source_valid[lane];
            if (source_valid[lane]) {
                sources[lane] = static_cast<uint32_t>(index);
                states[lane].gradient_mask &= ~(uint64_t{1} << sources[lane]);
            }
        }
        for (size_t lane = 0; aligned && lane < states.size(); ++lane) {
            const uint32_t source = sources[lane];
            for (unsigned component = 0; component < 4u; ++component) {
                states[lane].gradients_x[source][component] =
                    states[lane | 1u].registers[sources[lane | 1u]][component] -
                    states[lane & ~size_t{1}].registers[sources[lane & ~size_t{1}]][component];
                states[lane].gradients_y[source][component] =
                    states[lane | 2u].registers[sources[lane | 2u]][component] -
                    states[lane & ~size_t{2}].registers[sources[lane & ~size_t{2}]][component];
            }
            states[lane].gradient_mask |= uint64_t{1} << source;
        }
        for (size_t lane = 0; lane < states.size(); ++lane) if (ready[lane]) {
            auto& cursor = cursors[lane];
            shader.Fetch(program.alu[cursor.address], regs, config, texels, states[lane], cursor.predicate);
            ++cursor.address;
            --cursor.remaining;
            cursor.sequence >>= 2;
        }
    }
}

}
