#include "imx6_vivante_fe.h"

#include <cerfcore/core/byte_order.h>

#include <cstring>

namespace cerfcore {

namespace imx6_vivante {

namespace {

constexpr uint32_t kGlPipeSelect = 0x03800u;
constexpr uint32_t kFeCommandAddress = 0x654u;
constexpr uint32_t kFeCurrentAddress = 0x664u;
constexpr char kFeWaitDelayZero[] = "kFeWaitDelayZero";
constexpr char kFeRingAddress[] = "kFeRingAddress";
constexpr char kFePrefetchWindow[] = "kFePrefetchWindow";

}

bool VivanteFe::CacheParkedWord() {
    parked_word_host_ = mem_.TranslateCommandToHost(s_.fe_ring_pc_, sizeof(uint32_t));
    if (!parked_word_host_) return false;
    parked_word_ = cerf::le::U32(parked_word_host_);
    parked_pte_host_ = mem_.CommandPageTableEntryToHost(s_.fe_ring_pc_);
    if (parked_pte_host_) parked_pte_ = cerf::le::U32(parked_pte_host_);
    return true;
}

bool VivanteFe::ParkedWordUnchanged() const {
    return parked_ && (!parked_pte_host_ || cerf::le::U32(parked_pte_host_) == parked_pte_) &&
           cerf::le::U32(parked_word_host_) == parked_word_;
}

uint64_t VivanteFe::FrontendWaitCycles() {
    if (s_.fe_waiting_) return s_.fe_wait_cycles_;
    if (s_.fe_idle_ring_) {
        uint32_t word = 0u;
        if (mem_.ReadCommandWords(s_.fe_ring_pc_, &word, 1u) && (word >> 27) == kFeWait) return word & 0xFFFFu;
    }
    return 0u;
}

bool VivanteFe::PrepareFrontendFetch() {
    if (!s_.fe_live_) {
        SetParked(false);
        return false;
    }
    if (s_.fe_stalled_ || (!s_.fe_waiting_ && !s_.fe_idle_ring_)) return false;
    SetParked(s_.fe_idle_ring_ && CacheParkedWord());
    return true;
}

bool VivanteFe::ResumeParkedWord() {
    if (ParkedWordUnchanged()) return true;
    AdvanceFrontendRing();
    return false;
}

bool VivanteFe::AdvanceSemaphoreStall() {
    if (!s_.fe_live_ || !s_.fe_stalled_) return false;
    AdvanceFrontendRing();
    return true;
}

bool VivanteFe::TryParkIdleRing() {
    IdleRingInfo idle;
    if (!mem_.DetectIdleRing(s_.fe_ring_pc_, idle)) return false;
    ParkIdleRing(idle);
    return true;
}

void VivanteFe::ParkIdleRing(const IdleRingInfo& idle) {
    s_.fe_ring_pc_ = idle.target;
    s_.fe_ring_prefetch_ = idle.prefetch;
    s_.fe_idle_ring_ = true;
    s_.regs_[kFeCurrentAddress >> 2] = s_.fe_ring_pc_;
}

bool VivanteFe::FeLoopDetector::Equal(const FeControlState& lhs, const FeControlState& rhs) {
    if (lhs.pc != rhs.pc || lhs.window_words != rhs.window_words || lhs.call_depth != rhs.call_depth) return false;
    for (uint32_t i = 0u; i < lhs.call_depth; ++i) {
        if (lhs.call_stack[i].return_address != rhs.call_stack[i].return_address ||
            lhs.call_stack[i].return_window_words != rhs.call_stack[i].return_window_words) {
            return false;
        }
    }
    return true;
}

VivanteFe::FeLoopDetector::Result VivanteFe::FeLoopDetector::Insert(const FeControlState& state) {
    for (const FeControlState& entry : entries_) {
        if (Equal(entry, state)) return Result::Repeated;
    }
    entries_.push_back(state);
    return Result::Inserted;
}

void VivanteFe::AdvanceFrontendRing() {
    if (!s_.fe_live_) return;
    s_.fe_stalled_ = false;
    if (s_.fe_ring_pc_ == 0u) mem_.HaltUnsupported(kFeRingAddress, s_.fe_ring_pc_, 0u);

    if (s_.fe_waiting_) {
        s_.fe_waiting_ = false;
        s_.fe_wait_cycles_ = 0u;
        s_.fe_ring_pc_ += 8u;
    }

    if (s_.fe_idle_ring_) {
        uint32_t w0 = 0u;
        if (!mem_.ReadCommandWords(s_.fe_ring_pc_, &w0, 1u)) {
            mem_.HaltUnsupported("imx6-vivante front-end ring fetch outside GPU-mapped memory", s_.fe_ring_pc_, 1u);
        }
        const uint32_t op = w0 >> 27;
        if (op != kFeWait) {
            s_.fe_idle_ring_ = false;
            s_.fe_window_words_ = FePrefetchDwords(s_.fe_ring_prefetch_);
        }
    }

    if (s_.fe_idle_ring_) {
        if (TryParkIdleRing()) return;
        s_.fe_idle_ring_ = false;
        s_.fe_ring_pc_ += 8u;
        s_.fe_window_words_ = FePrefetchDwords(s_.fe_ring_prefetch_);
        s_.regs_[kFeCurrentAddress >> 2] = s_.fe_ring_pc_;
    }

    if (TryParkIdleRing()) return;

    FeStats stats;
    const uint32_t start_pc = s_.fe_ring_pc_;
    const uint32_t final_pc = ExecuteCommandStream(start_pc, stats);
    s_.regs_[kFeCurrentAddress >> 2] = final_pc;

    ApplyRunResult(stats, final_pc, start_pc);
}

void VivanteFe::RunFrontend(uint32_t control) {
    const uint32_t prefetch = control & kFeCommandPrefetchMask;
    const uint32_t address = s_.regs_[kFeCommandAddress >> 2];
    if (prefetch == 0u) mem_.HaltUnsupported(kFePrefetchWindow, kFeCommandAddress, control);
    s_.fe_stalled_ = false;
    s_.fe_waiting_ = false;
    s_.fe_wait_cycles_ = 0u;
    s_.fe_call_depth_ = 0u;
    IdleRingInfo idle;
    if (prefetch >= 2u && mem_.DetectIdleRing(address, idle)) {
        s_.regs_[kFeCurrentAddress >> 2] = idle.target;
        s_.fe_live_ = true;
        s_.fe_ring_pc_ = idle.target;
        s_.fe_ring_prefetch_ = idle.prefetch;
        s_.fe_window_words_ = 0u;
        s_.fe_idle_ring_ = true;
        return;
    }

    s_.fe_live_ = true;
    s_.fe_idle_ring_ = false;
    s_.fe_ring_pc_ = address;
    s_.fe_ring_prefetch_ = prefetch;
    s_.fe_window_words_ = FePrefetchDwords(prefetch);
    FeStats stats;
    const uint32_t final_pc = ExecuteCommandStream(address, stats);
    s_.regs_[kFeCurrentAddress >> 2] = final_pc;

    ApplyRunResult(stats, final_pc, address);
}

void VivanteFe::ApplyRunResult(const FeStats& stats, uint32_t final_pc, uint32_t fallback_pc) {
    if (stats.blocked) {
        s_.fe_ring_pc_ = final_pc;
        s_.fe_live_ = true;
        s_.fe_idle_ring_ = false;
    } else if (stats.idle_ring) {
        s_.fe_ring_pc_ = final_pc ? final_pc : fallback_pc;
        s_.fe_idle_ring_ = true;
    } else if (stats.stopped) {
        s_.fe_live_ = false;
        s_.fe_idle_ring_ = false;
    } else {
        s_.fe_ring_pc_ = final_pc;
        s_.fe_live_ = true;
        s_.fe_idle_ring_ = false;
    }
}

void VivanteFe::CommandFits(uint32_t command_words, uint32_t window_words, uint32_t pc) {
    if (command_words <= window_words) return;
    mem_.HaltUnsupported(kFePrefetchWindow, pc, command_words);
}

void VivanteFe::FetchWords(uint32_t pc, uint32_t* words, uint32_t count) {
    if (!mem_.FetchCommandWords(pc, words, count))
        mem_.HaltUnsupported("imx6-vivante front-end command fetch outside GPU-mapped memory", pc, count);
}

void VivanteFe::ConsumeWords(uint32_t words, uint32_t& pc, uint32_t& window_words) const {
    pc += words * 4u;
    window_words -= words;
}

uint32_t VivanteFe::ExecuteCommandStream(uint32_t address, FeStats& stats) {
    uint32_t pc = address;
    uint32_t window_words = s_.fe_window_words_;
    loop_detector_.Reset();

    while (true) {
        CommandFits(2u, window_words, pc);

        s_.regs_[kFeCurrentAddress >> 2] = pc;
        uint32_t words[4]{};
        FetchWords(pc, words, 2u);

        const uint32_t cmd = words[0];
        const uint32_t op = cmd >> 27;

        if (op == kFeLink || op == kFeCall || op == kFeReturn) {
            FeControlState state;
            state.pc = pc;
            state.window_words = window_words;
            state.call_depth = s_.fe_call_depth_;
            for (uint32_t i = 0u; i < state.call_depth; ++i) state.call_stack[i] = s_.fe_call_stack_[i];
            const FeLoopDetector::Result result = loop_detector_.Insert(state);
            if (result == FeLoopDetector::Result::Repeated)
                mem_.HaltUnsupported("Vivante FE command stream loops without WAIT, not modelled", pc, cmd);
        }

        if ((op == kFeWait && window_words >= 4u) || op == kFeLink) {
            IdleRingInfo idle;
            if (mem_.DetectIdleRing(pc, idle)) {
                stats.idle_ring = true;
                s_.fe_ring_prefetch_ = idle.prefetch;
                s_.fe_window_words_ = 0u;
                s_.fe_call_depth_ = 0u;
                return idle.target;
            }
        }

        switch (op) {
        case kFeLoadState: {
            uint32_t count = (cmd >> 16) & 0x3FFu;
            if (count == 0u) count = 1024u;
            const uint32_t state_word = cmd & 0xFFFFu;
            const bool fixp = (cmd & (1u << 26)) != 0u;
            const uint32_t len = (1u + count + 1u) & ~1u;
            CommandFits(len, window_words, pc);
            FetchWords(pc, packet_buffer_.data(), len);
            ApplyLoadState(packet_buffer_.data() + 1u, state_word, count, fixp);
            ConsumeWords(len, pc, window_words);
            break;
        }
        case kFeEnd:
            if (cmd & 0x00000100u) {
                const uint32_t event_id = cmd & 0x1Fu;
                mem_.RaiseEvent(event_id);
            }
            stats.stopped = true;
            s_.fe_waiting_ = false;
            s_.fe_wait_cycles_ = 0u;
            s_.fe_window_words_ = 0u;
            s_.fe_call_depth_ = 0u;
            return pc + 4u;

        case kFeNop:
        case kFeChipSelect:
        case kFeSnapPages: {
            const uint32_t len = FeAlignedCommandWords(1u);
            CommandFits(len, window_words, pc);
            if (op == kFeChipSelect)
                mem_.HaltUnsupported("imx6-vivante CHIPSELECT is not modelled", pc, cmd);
            if (op == kFeSnapPages)
                mem_.HaltUnsupported("imx6-vivante SNAP_PAGES is not modelled", pc, cmd);
            ConsumeWords(len, pc, window_words);
            break;
        }

        case kFeWait: {
            constexpr uint32_t len = 2u;
            CommandFits(len, window_words, pc);
            const uint32_t wait_cycles = cmd & 0xFFFFu;
            if (wait_cycles == 0u) {
                mem_.HaltUnsupported(kFeWaitDelayZero, pc, cmd);
            }
            loop_detector_.Reset();
            s_.fe_waiting_ = true;
            s_.fe_wait_cycles_ = wait_cycles;
            s_.fe_ring_pc_ = pc;
            s_.fe_window_words_ = window_words - len;
            return pc;
        }

        case kFeDraw2d: {
            if ((mem_.StateReg(kGlPipeSelect) & 1u) == 0u)
                mem_.HaltUnsupported("imx6-vivante DRAW_2D with 3D pipe selected", pc, cmd);
            if (!mem_.Is2d()) mem_.HaltUnsupported("imx6-vivante DRAW_2D on non-GC320 core", pc, cmd);
            const uint32_t rects = (cmd >> 8) & 0xFFu;
            if (rects == 0u) mem_.HaltUnsupported("kDraw2dZeroRectangleCount", pc, cmd);
            const uint32_t data_count = (cmd >> 16) & 0x7FFu;
            const uint32_t len = FeDraw2dPacketWords(rects, data_count);
            CommandFits(len, window_words, pc);

            FetchWords(pc, packet_buffer_.data(), len);
            blit_.ExecuteDraw2d(packet_buffer_.data() + 2u, rects,
                               packet_buffer_.data() + 2u + rects * 2u, data_count);
            ConsumeWords(len, pc, window_words);
            break;
        }

        case kFeDrawPrimitives:
            mem_.HaltUnsupported("imx6-vivante DRAW_PRIMITIVES is not rendered", pc, cmd);
        case kFeDrawIndexedPrimitives:
            mem_.HaltUnsupported("imx6-vivante DRAW_INDEXED is not rendered", pc, cmd);

        case kFeLink: {
            constexpr uint32_t len = 2u;
            CommandFits(len, window_words, pc);
            const uint32_t link_prefetch = cmd & 0xFFFFu;
            const uint32_t link_address = words[1];
            if (link_prefetch == 0u)
                mem_.HaltUnsupported("imx6-vivante LINK with zero prefetch", pc, cmd);
            pc = link_address;
            window_words = FePrefetchDwords(link_prefetch);
            break;
        }

        case kFeStall: {
            constexpr uint32_t len = 2u;
            CommandFits(len, window_words, pc);
            const uint32_t token = words[1];
            if (!mem_.TryConsumeSemaphoreToken(token)) {
                stats.blocked = true;
                s_.fe_stalled_ = true;
                s_.fe_waiting_ = false;
                s_.fe_wait_cycles_ = 0u;
                s_.fe_window_words_ = window_words;
                return pc;
            }
            ConsumeWords(len, pc, window_words);
            loop_detector_.Reset();
            break;
        }

        case kFeWaitFence: {
            mem_.HaltUnsupported("imx6-vivante WAIT_FENCE is marked HALTI5", pc, cmd);
        }

        case kFeCall: {
            constexpr uint32_t len = 4u;
            CommandFits(len, window_words, pc);
            FetchWords(pc, words, len);
            if (s_.fe_call_depth_ >= kFeCallStackDepth)
                mem_.HaltUnsupported("imx6-vivante FE CALL stack overflow", pc, cmd);
            if ((cmd & kFeCommandPrefetchMask) == 0u)
                mem_.HaltUnsupported("imx6-vivante CALL with zero prefetch", pc, cmd);
            s_.fe_call_stack_[s_.fe_call_depth_++] = {words[3], FePrefetchDwords(words[2] & 0xFFFFu)};
            pc = words[1];
            window_words = FePrefetchDwords(cmd & 0xFFFFu);
            break;
        }

        case kFeReturn:
            if (s_.fe_call_depth_ == 0u) mem_.HaltUnsupported("imx6-vivante FE RETURN stack underflow", pc, cmd);
            {
                const FeCallFrame frame = s_.fe_call_stack_[--s_.fe_call_depth_];
                if (frame.return_window_words == 0u)
                    mem_.HaltUnsupported("imx6-vivante RETURN with zero prefetch", pc, frame.return_address);
                pc = frame.return_address;
                window_words = frame.return_window_words;
            }
            break;

        case kFeDrawInstanced:
            mem_.HaltUnsupported("imx6-vivante DRAW_INSTANCED is not rendered", pc, cmd);
        case kFeDrawIndirect:
            mem_.HaltUnsupported("imx6-vivante DRAW_INDIRECT is not rendered", pc, cmd);
        default: mem_.HaltUnsupported("imx6-vivante FE unknown opcode", pc, cmd);
        }
    }
}

void VivanteFe::ApplyLoadState(const uint32_t* values, uint32_t state_word, uint32_t count, bool fixp) {
    mem_.EnsureStateSize();
    VivanteBlitKick kick;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t v = values[i];
        if (fixp) {
            const float converted = static_cast<float>(static_cast<int32_t>(v)) / 65536.0f;
            std::memcpy(&v, &converted, sizeof(v));
        }
        const uint32_t byte_off = (state_word + i) << 2;
        mem_.StoreStateReg(byte_off, v);
        VivanteBlit::DecodeKick(byte_off, v, kick);
    }
    blit_.ExecuteKick(kick);
}

}

}
