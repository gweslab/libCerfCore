#include "cortex_m3_core.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace cortex_m3 {

[[noreturn]] void Raise(Bus& bus, const char* what, uint32_t detail) {
    bus.Fault(what, detail);
    CERF_UNREACHABLE();
}

[[noreturn]] void Core::UnalignedFault(Bus& bus) {
    TakeUsageFault(bus, kCfsrUnaligned);
}

[[noreturn]] void Core::Undefined(Bus& bus, uint32_t) {
    TakeUsageFault(bus, kCfsrUndefInstr);
}

[[noreturn]] void Core::Coprocessor(Bus& bus, uint16_t hw1, uint16_t hw2) {
    const uint32_t op1 = (hw1 >> 4) & 0x3Fu;
    if ((op1 & 0x30u) == 0x30u || (op1 & 0x3Eu) == 0u) Undefined(bus, (static_cast<uint32_t>(hw1) << 16) | hw2);
    TakeUsageFault(bus, kCfsrNoCp);
}

[[noreturn]] void Core::Unpredictable(Bus& bus, uint32_t encoding) {
    Raise(bus, "Cortex-M3: UNPREDICTABLE instruction encoding", encoding);
}

void Core::Reset(Bus& bus, uint32_t vector_table) {
    const uint64_t cycles = state_.cycles;
    state_ = State{};
    state_.cycles = cycles;
    unprivileged_access_ = false;
    MarkExceptionsDirty();
    state_.sp_main = bus.Read(vector_table, 4u) & 0xFFFFFFFCu;
    state_.lr = 0xFFFFFFFFu;
    const uint32_t entry = bus.Read(vector_table + 4u, 4u);
    state_.thumb = (entry & 1u) != 0u;
    state_.pc = entry & 0xFFFFFFFEu;
    InvalidateCode(code_base_, code_size_);
}

void Core::SetInterruptLines(uint32_t lines) {
    interrupt_lines_ = lines;
}

void Core::SetExternalLevel(uint32_t irq, bool asserted) {
    const uint32_t exception = kExcExternalBase + irq;
    if ((state_.level[exception] != 0u) == asserted) return;
    const bool was_pending = IsPending(exception);
    state_.level[exception] = asserted ? 1u : 0u;
    if (asserted) state_.pending[exception] = 1u;
    NoteEnteredPending(was_pending, exception);
    MarkExceptionsDirty();
}

bool Core::Privileged() const {
    return state_.handler_mode || (state_.control & kControlNPriv) == 0u;
}

bool Core::ConditionPassed(uint32_t cond) const {
    const bool n = (state_.apsr & kPsrN) != 0u;
    const bool z = (state_.apsr & kPsrZ) != 0u;
    const bool c = (state_.apsr & kPsrC) != 0u;
    const bool v = (state_.apsr & kPsrV) != 0u;
    bool result = true;
    switch ((cond >> 1) & 7u) {
    case 0u: result = z; break;
    case 1u: result = c; break;
    case 2u: result = n; break;
    case 3u: result = v; break;
    case 4u: result = c && !z; break;
    case 5u: result = n == v; break;
    case 6u: result = (n == v) && !z; break;
    default: result = true; break;
    }
    if ((cond & 1u) != 0u && cond != 0xFu) result = !result;
    return result;
}

uint32_t Core::CurrentCondition() const {
    return InItBlock() ? (state_.itstate >> 4) & 0xFu : 0xEu;
}

void Core::ItAdvance() {
    if ((state_.itstate & 0x07u) == 0u) {
        state_.itstate = 0u;
    } else {
        state_.itstate = static_cast<uint8_t>((state_.itstate & 0xE0u) | ((state_.itstate << 1) & 0x1Fu));
    }
}

void Core::SetNz(uint32_t result) {
    state_.apsr &= ~(kPsrN | kPsrZ);
    if ((result & 0x80000000u) != 0u) state_.apsr |= kPsrN;
    if (result == 0u) state_.apsr |= kPsrZ;
}

void Core::SetNzc(uint32_t result, bool carry) {
    SetNz(result);
    state_.apsr = carry ? (state_.apsr | kPsrC) : (state_.apsr & ~kPsrC);
}

void Core::SetNzcv(uint32_t result, bool carry, bool overflow) {
    SetNzc(result, carry);
    state_.apsr = overflow ? (state_.apsr | kPsrV) : (state_.apsr & ~kPsrV);
}

uint32_t Core::AddWithCarry(uint32_t x, uint32_t y, bool carry_in, bool* carry_out, bool* overflow) {
    const uint64_t unsigned_sum = static_cast<uint64_t>(x) + y + (carry_in ? 1u : 0u);
    const int64_t signed_sum = static_cast<int64_t>(static_cast<int32_t>(x)) +
                               static_cast<int32_t>(y) + (carry_in ? 1 : 0);
    const uint32_t result = static_cast<uint32_t>(unsigned_sum);
    if (carry_out) *carry_out = static_cast<uint64_t>(result) != unsigned_sum;
    if (overflow) *overflow = static_cast<int64_t>(static_cast<int32_t>(result)) != signed_sum;
    return result;
}

ShiftResult Core::ShiftC(uint32_t value, ShiftType type, uint32_t amount, bool carry_in) {
    if (amount == 0u) return {value, carry_in};
    switch (type) {
    case ShiftType::Lsl:
        if (amount >= 33u) return {0u, false};
        if (amount == 32u) return {0u, (value & 1u) != 0u};
        return {value << amount, ((value >> (32u - amount)) & 1u) != 0u};
    case ShiftType::Lsr:
        if (amount >= 33u) return {0u, false};
        if (amount == 32u) return {0u, (value >> 31) != 0u};
        return {value >> amount, ((value >> (amount - 1u)) & 1u) != 0u};
    case ShiftType::Asr: {
        const int32_t s = static_cast<int32_t>(value);
        if (amount >= 32u) {
            const uint32_t fill = s < 0 ? 0xFFFFFFFFu : 0u;
            return {fill, s < 0};
        }
        return {static_cast<uint32_t>(s >> amount), ((value >> (amount - 1u)) & 1u) != 0u};
    }
    case ShiftType::Ror: {
        const uint32_t m = amount % 32u;
        const uint32_t result = m == 0u ? value : (value >> m) | (value << (32u - m));
        return {result, (result >> 31) != 0u};
    }
    case ShiftType::Rrx:
        return {(carry_in ? 0x80000000u : 0u) | (value >> 1), (value & 1u) != 0u};
    }
    return {value, carry_in};
}

void Core::DecodeImmShift(uint32_t type, uint32_t imm5, ShiftType* shift_t, uint32_t* shift_n) {
    switch (type & 3u) {
    case 0u: *shift_t = ShiftType::Lsl; *shift_n = imm5; break;
    case 1u: *shift_t = ShiftType::Lsr; *shift_n = imm5 == 0u ? 32u : imm5; break;
    case 2u: *shift_t = ShiftType::Asr; *shift_n = imm5 == 0u ? 32u : imm5; break;
    default:
        if (imm5 == 0u) {
            *shift_t = ShiftType::Rrx;
            *shift_n = 1u;
        } else {
            *shift_t = ShiftType::Ror;
            *shift_n = imm5;
        }
        break;
    }
}

bool Core::ThumbExpandImmUnpredictable(uint32_t imm12) {
    return ((imm12 >> 10) & 3u) == 0u && ((imm12 >> 8) & 3u) != 0u && (imm12 & 0xFFu) == 0u;
}

ShiftResult Core::ThumbExpandImmC(uint32_t imm12) const {
    const uint32_t low = imm12 & 0xFFu;
    if (((imm12 >> 10) & 3u) == 0u) {
        uint32_t imm32 = 0u;
        switch ((imm12 >> 8) & 3u) {
        case 0u: imm32 = low; break;
        case 1u: imm32 = (low << 16) | low; break;
        case 2u: imm32 = (low << 24) | (low << 8); break;
        default: imm32 = (low << 24) | (low << 16) | (low << 8) | low; break;
        }
        return {imm32, FlagC()};
    }
    const uint32_t unrotated = 0x80u | (imm12 & 0x7Fu);
    return ShiftC(unrotated, ShiftType::Ror, (imm12 >> 7) & 0x1Fu, FlagC());
}

void Core::BranchWritePc(uint32_t address) {
    state_.pc = address & 0xFFFFFFFEu;
    branched_ = true;
    ++cycles_;
}

void Core::BxWritePc(Bus& bus, uint32_t address) {
    if (IsExceptionReturn(address)) {
        ExceptionReturn(bus, address & 0x0FFFFFFFu);
        return;
    }
    state_.thumb = (address & 1u) != 0u;
    state_.pc = address & 0xFFFFFFFEu;
    branched_ = true;
    cycles_ += 2u;
}

void Core::BlxWritePc(Bus&, uint32_t address) {
    state_.thumb = (address & 1u) != 0u;
    state_.pc = address & 0xFFFFFFFEu;
    branched_ = true;
    cycles_ += 2u;
}

void Core::MapMemory(uint32_t base, uint32_t size, uint8_t* host, bool writable) {
    if (direct_count_ < 2u) direct_[direct_count_++] = DirectRegion{base, size, host, writable};
    if (!writable && code_.empty()) {
        code_base_ = base;
        code_size_ = size;
        code_limit_ = size - 2u;
        code_host_ = host;
        code_.resize(size / 2u);
    }
}

void Core::TrackWrites(uint32_t base, uint64_t* dirty_pages) {
    for (uint32_t i = 0u; i < direct_count_; ++i) {
        if (direct_[i].base == base) direct_[i].dirty = dirty_pages;
    }
}

void Core::InvalidateCode(uint32_t address, uint32_t size) {
    const uint32_t first = (address & ~1u) - code_base_ >= 2u ? (address & ~1u) - 2u : address & ~1u;
    for (uint32_t a = first; a < address + size; a += 2u) {
        if (a - code_base_ < code_size_) code_[(a - code_base_) / 2u].handler = nullptr;
    }
}

void Core::CheckDeviceAlignment(Bus& bus, uint32_t address, uint32_t size) {
    const bool normal = address < 0x40000000u || (address >= 0x60000000u && address < 0xA0000000u);
    if (!normal && (address & (size - 1u)) != 0u)
        Raise(bus, "Cortex-M3: UNPREDICTABLE unaligned access to Device or Strongly-ordered memory", address);
}

uint32_t Core::MemReadDevice(Bus& bus, uint32_t address, uint32_t size) {
    CheckDeviceAlignment(bus, address, size);
    if (IsScs(address)) return ScsRead(bus, address, size);
    return bus.Read(address, size);
}

void Core::MemWriteDevice(Bus& bus, uint32_t address, uint32_t size, uint32_t value) {
    CheckDeviceAlignment(bus, address, size);
    if (IsScs(address)) {
        ScsWrite(bus, address, size, value);
        return;
    }
    bus.Write(address, size, value);
}

uint16_t Core::FetchSlow(Bus& bus, uint32_t address) {
    return static_cast<uint16_t>(bus.Read(address, 2u));
}

uint32_t Core::Step(Bus& bus) {
    committed_ = 0u;
    if (state_.sleep != kSleepNone) {
        if (!SleepEnds(bus)) return Idle();
        if (state_.sleep_exc_return != 0u && PendingToTake() == 0u) return ResumeSleepOnExitReturn(bus);
    }
    const uint32_t exception = PendingToTake();
    if (exception != 0u) {
        cycles_ = TakePendingException(bus, exception);
        CommitCycles();
        return cycles_;
    }
    CheckIciInstruction(bus);
    prev_load_regs_ = state_.load_regs;
    state_.load_regs = 0u;
    if (!state_.thumb) {
        this_instr_ = state_.pc;
        cycles_ = 0u;
        TakeUsageFault(bus, kCfsrInvState);
    }

    const uint32_t offset = state_.pc - code_base_;
    if (offset < code_limit_ && !InItBlock()) return StepDecoded(bus, code_[offset / 2u]);
    return StepGeneric(bus);
}

void Core::Run(Bus& bus, uint64_t cycle) {
    run_target_ = cycle;
    run_limit_ = cycle;
    while (state_.cycles < run_limit_) {
        try {
            while (state_.cycles < run_limit_) Step(bus);
        } catch (const SynchronousFault&) {
            CommitCycles();
        }
    }
}

uint32_t Core::StepDecoded(Bus& bus, Decoded& decoded) {
    this_instr_ = state_.pc;
    if (decoded.handler == nullptr) Decode(decoded, this_instr_);
    next_instr_ = this_instr_ + decoded.size;
    cycles_ = 1u;
    branched_ = false;
    decoded.handler(*this, bus, decoded);
    if (!branched_) state_.pc = next_instr_;
    CommitCycles();
    return cycles_;
}

uint32_t Core::StepGeneric(Bus& bus) {
    this_instr_ = state_.pc;
    const uint16_t hw1 = Fetch16(bus, this_instr_);
    const bool wide = (hw1 >> 11) >= 0x1Du;
    const uint16_t hw2 = wide ? Fetch16(bus, this_instr_ + 2u) : 0u;
    next_instr_ = this_instr_ + (wide ? 4u : 2u);
    cycles_ = 1u;
    branched_ = false;
    exception_event_ = false;

    const bool in_it = InItBlock();
    const bool executes = !in_it || ConditionPassed(CurrentCondition()) || (!wide && (hw1 & 0xFF00u) == 0xBE00u);
    if (executes) {
        if (wide) {
            Execute32(bus, hw1, hw2);
        } else {
            Execute16(bus, hw1);
        }
    }
    if (!branched_) state_.pc = next_instr_;
    if (in_it && !exception_event_) ItAdvance();

    CommitCycles();
    return cycles_;
}

void Core::Execute32(Bus& bus, uint16_t hw1, uint16_t hw2) {
    const uint32_t op1 = (hw1 >> 11) & 3u;
    const uint32_t op2 = (hw1 >> 4) & 0x7Fu;
    const bool op = (hw2 & 0x8000u) != 0u;
    if (op1 == 1u) {
        if ((op2 & 0x64u) == 0x00u) return ExecuteLoadStoreMultiple(bus, hw1, hw2);
        if ((op2 & 0x64u) == 0x04u) return ExecuteDualExclusiveTable(bus, hw1, hw2);
        if ((op2 & 0x60u) == 0x20u) return ExecuteDataShiftedReg(bus, hw1, hw2);
        Coprocessor(bus, hw1, hw2);
    }
    if (op1 == 2u) {
        if (op) return ExecuteBranchMisc(bus, hw1, hw2);
        if ((op2 & 0x20u) == 0u) return ExecuteDataModifiedImm(bus, hw1, hw2);
        return ExecuteDataPlainImm(bus, hw1, hw2);
    }
    if ((op2 & 0x71u) == 0x00u) return ExecuteStoreSingle(bus, hw1, hw2);
    if ((op2 & 0x67u) == 0x01u || (op2 & 0x67u) == 0x03u || (op2 & 0x67u) == 0x05u)
        return ExecuteLoadSingle(bus, hw1, hw2);
    if ((op2 & 0x70u) == 0x20u) return ExecuteDataRegister(bus, hw1, hw2);
    if ((op2 & 0x78u) == 0x30u) return ExecuteMultiply(bus, hw1, hw2);
    if ((op2 & 0x78u) == 0x38u) return ExecuteLongMultiplyDivide(bus, hw1, hw2);
    if ((op2 & 0x40u) != 0u) Coprocessor(bus, hw1, hw2);
    Undefined(bus, (static_cast<uint32_t>(hw1) << 16) | hw2);
}

void Core::SaveState(StateWriter& writer) const {
    static_assert(StateVisitCoversAllBytes<State>(
                      [](State& s, StateFieldBytes& f) { State::Visit(s, f); }),
                  "State::Visit must name or skip every field of State");
    StateWriteField field(writer);
    State saved = state_;
    State::Visit(saved, field);
}

void Core::RestoreState(StateReader& reader) {
    StateReadField field(reader);
    State::Visit(state_, field);
    MarkExceptionsDirty();
}

}

}
