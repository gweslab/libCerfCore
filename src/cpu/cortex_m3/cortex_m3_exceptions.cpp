#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

namespace {

constexpr uint32_t kPopCycles = 8u;

uint32_t ItToPsr(uint8_t it) {
    return (static_cast<uint32_t>(it & 3u) << 25) | (static_cast<uint32_t>(it >> 2) << 10);
}

uint8_t PsrToIt(uint32_t psr) {
    return static_cast<uint8_t>(((psr >> 25) & 3u) | (((psr >> 10) & 0x3Fu) << 2));
}

}

int Core::ExceptionPriority(uint32_t exception) const {
    if (exception == kExcReset) return -3;
    if (exception == kExcNmi) return -2;
    if (exception == kExcHardFault) return -1;
    return state_.priority[exception];
}

int Core::ExecutionPriority(bool primask_applies) const {
    const int groupvalue = 2 << state_.prigroup;
    int highestpri = 256;
    for (uint32_t i = 2u; i < kMaxExceptions; ++i) {
        if (state_.active[i] == 0u) continue;
        const int pri = ExceptionPriority(i);
        if (pri < highestpri) {
            highestpri = pri;
            if (highestpri >= 0) highestpri -= highestpri % groupvalue;
        }
    }
    int boostedpri = BoostedPriority();
    if (state_.primask != 0u && primask_applies && boostedpri > 0) boostedpri = 0;
    return boostedpri < highestpri ? boostedpri : highestpri;
}

int Core::BoostedPriority() const {
    const int groupvalue = 2 << state_.prigroup;
    int boostedpri = 256;
    if (state_.basepri != 0u) {
        boostedpri = state_.basepri;
        boostedpri -= boostedpri % groupvalue;
    }
    if (state_.faultmask != 0u) boostedpri = -1;
    return boostedpri;
}

bool Core::IsPending(uint32_t exception) const {
    return state_.pending[exception] != 0u || (state_.level[exception] != 0u && state_.active[exception] == 0u);
}

uint32_t Core::HighestPending() const {
    uint32_t best = 0u;
    int best_priority = 0;
    for (uint32_t chunk = 0u; chunk < kMaxExceptions; chunk += 8u) {
        uint64_t pending = 0u;
        uint64_t level = 0u;
        std::memcpy(&pending, &state_.pending[chunk], 8u);
        std::memcpy(&level, &state_.level[chunk], 8u);
        if ((pending | level) == 0u) continue;
        for (uint32_t i = chunk < 2u ? 2u : chunk; i < chunk + 8u; ++i) {
            if (!IsPending(i)) continue;
            if (i >= kExcExternalBase && state_.enabled[i] == 0u) continue;
            const int pri = ExceptionPriority(i);
            if (best == 0u || pri < best_priority) {
                best = i;
                best_priority = pri;
            }
        }
    }
    return best;
}

int Core::GroupPriority(uint32_t exception) const {
    const int groupvalue = 2 << state_.prigroup;
    int group = ExceptionPriority(exception);
    if (group >= 0) group -= group % groupvalue;
    return group;
}

uint32_t Core::ComputePendingToTake() const {
    const uint32_t best = HighestPending();
    if (best == 0u) return 0u;
    return GroupPriority(best) < ExecutionPriority() ? best : 0u;
}

void Core::NoteEnteredPending(bool was_pending, uint32_t exception) {
    if (!was_pending && IsPending(exception) && (state_.scr & kScrSevOnPend) != 0u) state_.event_register = 1u;
}

void Core::SetPending(uint32_t exception) {
    const bool was_pending = IsPending(exception);
    state_.pending[exception] = 1u;
    NoteEnteredPending(was_pending, exception);
}

uint32_t Core::ActiveCount() const {
    uint32_t count = 0u;
    for (uint32_t i = 0u; i < kMaxExceptions; ++i) count += state_.active[i] != 0u ? 1u : 0u;
    return count;
}

void Core::PushStack(Bus& bus) {
    const uint32_t framesize = 0x20u;
    const bool forcealign = (state_.ccr & (1u << 9)) != 0u;
    const uint32_t spmask = forcealign ? ~4u : ~0u;
    const bool process = !state_.handler_mode && (state_.control & kControlSpSel) != 0u;
    uint32_t& sp = process ? state_.sp_process : state_.sp_main;
    const uint32_t frameptralign = forcealign ? (sp >> 2) & 1u : 0u;
    sp = (sp - framesize) & spmask;
    const uint32_t frameptr = sp;
    const uint32_t xpsr = (state_.apsr & kApsrMask) | ItToPsr(state_.itstate) | (state_.thumb ? kPsrT : 0u) |
                          (state_.ipsr & kIpsrMask);
    MemWrite(bus, frameptr + 0x00u, 4u, state_.r[0]);
    MemWrite(bus, frameptr + 0x04u, 4u, state_.r[1]);
    MemWrite(bus, frameptr + 0x08u, 4u, state_.r[2]);
    MemWrite(bus, frameptr + 0x0Cu, 4u, state_.r[3]);
    MemWrite(bus, frameptr + 0x10u, 4u, state_.r[12]);
    MemWrite(bus, frameptr + 0x14u, 4u, state_.lr);
    MemWrite(bus, frameptr + 0x18u, 4u, state_.pc);
    MemWrite(bus, frameptr + 0x1Cu, 4u, (xpsr & ~(1u << 9)) | (frameptralign << 9));
    if (state_.handler_mode) {
        state_.lr = 0xFFFFFFF1u;
    } else {
        state_.lr = 0xFFFFFFF9u | ((state_.control & kControlSpSel) != 0u ? 4u : 0u);
    }
}

void Core::ExceptionEntry(Bus& bus, uint32_t exception) {
    PushStack(bus);
    ExceptionTaken(bus, exception);
}

void Core::ExceptionTaken(Bus& bus, uint32_t exception) {
    const uint32_t vector = MemRead(bus, state_.vtor + 4u * exception, 4u);
    state_.pc = vector & 0xFFFFFFFEu;
    state_.thumb = (vector & 1u) != 0u;
    state_.handler_mode = true;
    state_.ipsr = exception;
    state_.itstate = 0u;
    state_.control = static_cast<uint8_t>(state_.control & ~kControlSpSel);
    state_.active[exception] = 1u;
    state_.pending[exception] = 0u;
    ClearExclusiveLocal();
    state_.event_register = 1u;
    exception_event_ = true;
    MarkExceptionsDirty();
}

void Core::DeActivate(uint32_t returning) {
    state_.active[returning] = 0u;
    if ((state_.ipsr & kIpsrMask) != kExcNmi) state_.faultmask = 0u;
}

void Core::ReturnUsageFault(Bus& bus, uint32_t exc_return) {
    state_.cfsr |= kCfsrInvPc;
    EscalateToHardFault(bus, kHfsrForced);
    state_.lr = 0xF0000000u | exc_return;
    ExceptionTaken(bus, kExcHardFault);
    branched_ = true;
}

void Core::ExceptionReturn(Bus& bus, uint32_t exc_return) {
    if ((exc_return & 0x0FFFFFF0u) != 0x0FFFFFF0u) Raise(bus, "Cortex-M3: reserved EXC_RETURN", exc_return);
    const uint32_t returning = state_.ipsr & kIpsrMask;
    if (returning >= kMaxExceptions) Raise(bus, "Cortex-M3: exception return with IPSR past the modelled exceptions", returning);
    if (state_.active[returning] == 0u) {
        DeActivate(returning);
        return ReturnUsageFault(bus, exc_return);
    }
    const uint32_t nested = ActiveCount();
    uint32_t frameptr = 0u;
    switch (exc_return & 0xFu) {
    case 0x1u:
        frameptr = state_.sp_main;
        state_.handler_mode = true;
        state_.control = static_cast<uint8_t>(state_.control & ~kControlSpSel);
        break;
    case 0x9u:
    case 0xDu:
        if (nested != 1u && (state_.ccr & 1u) == 0u) {
            DeActivate(returning);
            return ReturnUsageFault(bus, exc_return);
        }
        frameptr = (exc_return & 0xFu) == 0x9u ? state_.sp_main : state_.sp_process;
        state_.handler_mode = false;
        state_.control = (exc_return & 0xFu) == 0x9u ? static_cast<uint8_t>(state_.control & ~kControlSpSel)
                                                     : static_cast<uint8_t>(state_.control | kControlSpSel);
        break;
    default:
        DeActivate(returning);
        return ReturnUsageFault(bus, exc_return);
    }
    DeActivate(returning);
    MarkExceptionsDirty();
    const bool sleep_on_exit = !state_.handler_mode && ActiveCount() == 0u && (state_.scr & kScrSleepOnExit) != 0u;
    if (sleep_on_exit) {
        if (TailChained(bus, exc_return, 0u)) return;
        ClearExclusiveLocal();
        state_.event_register = 1u;
        state_.sleep_exc_return = exc_return;
        EnterSleep(bus, kSleepWfi);
        if (state_.sleep != kSleepNone) {
            branched_ = true;
            return;
        }
        state_.sleep_exc_return = 0u;
    }
    if (TailChained(bus, exc_return, kPopCycles)) return;
    CompleteReturn(bus, frameptr, exc_return);
}

uint32_t Core::ResumeSleepOnExitReturn(Bus& bus) {
    const uint32_t exc_return = state_.sleep_exc_return;
    state_.sleep_exc_return = 0u;
    cycles_ = 0u;
    CompleteReturn(bus, (exc_return & 0xFu) == 0xDu ? state_.sp_process : state_.sp_main, exc_return);
    CommitCycles();
    return cycles_;
}

void Core::CompleteReturn(Bus& bus, uint32_t frameptr, uint32_t exc_return) {
    PopStack(bus, frameptr, exc_return);
    const bool ipsr_zero = (state_.ipsr & kIpsrMask) == 0u;
    if (state_.handler_mode == ipsr_zero) {
        PushStack(bus);
        return ReturnUsageFault(bus, exc_return);
    }
    ClearExclusiveLocal();
    state_.event_register = 1u;
    branched_ = true;
    exception_event_ = true;
    MarkExceptionsDirty();
}

void Core::PopStack(Bus& bus, uint32_t frameptr, uint32_t exc_return) {
    const uint32_t framesize = 0x20u;
    const bool forcealign = (state_.ccr & (1u << 9)) != 0u;
    state_.r[0] = MemRead(bus, frameptr + 0x00u, 4u);
    state_.r[1] = MemRead(bus, frameptr + 0x04u, 4u);
    state_.r[2] = MemRead(bus, frameptr + 0x08u, 4u);
    state_.r[3] = MemRead(bus, frameptr + 0x0Cu, 4u);
    state_.r[12] = MemRead(bus, frameptr + 0x10u, 4u);
    state_.lr = MemRead(bus, frameptr + 0x14u, 4u);
    const uint32_t return_pc = MemRead(bus, frameptr + 0x18u, 4u);
    if ((return_pc & 1u) != 0u) Raise(bus, "Cortex-M3: UNPREDICTABLE exception return to a PC that is not halfword aligned", return_pc);
    state_.pc = return_pc;
    const uint32_t psr = MemRead(bus, frameptr + 0x1Cu, 4u);
    const uint32_t spmask = (((psr >> 9) & 1u) != 0u && forcealign) ? 4u : 0u;
    if ((exc_return & 0xFu) == 0xDu) {
        state_.sp_process = (state_.sp_process + framesize) | spmask;
    } else {
        state_.sp_main = (state_.sp_main + framesize) | spmask;
    }
    state_.apsr = psr & kApsrMask;
    state_.ipsr = psr & kIpsrMask;
    state_.thumb = (psr & kPsrT) != 0u;
    state_.itstate = PsrToIt(psr);
}

void Core::CallSupervisor(Bus& bus) {
    if (GroupPriority(kExcSvCall) >= ExecutionPriority()) {
        EscalateToHardFault(bus, kHfsrForced);
        return;
    }
    SetPending(kExcSvCall);
    MarkExceptionsDirty();
}

void Core::BreakpointInstruction(Bus& bus) {
    EscalateToHardFault(bus, kHfsrDebugEvt);
    state_.dfsr |= kDfsrBkpt;
    state_.pc = this_instr_;
    branched_ = true;
    exception_event_ = true;
}

void Core::TakeUsageFault(Bus& bus, uint32_t cfsr_bit) {
    state_.cfsr |= cfsr_bit;
    EscalateToHardFault(bus, kHfsrForced);
    state_.pc = this_instr_;
    throw SynchronousFault{};
}

void Core::EscalateToHardFault(Bus& bus, uint32_t hfsr_bits) {
    if (ExecutionPriority() < 0)
        Raise(bus, "Cortex-M3: Lockup, a synchronous fault, SVC or BKPT at a priority of -1 or higher", this_instr_);
    state_.hfsr |= hfsr_bits;
    SetPending(kExcHardFault);
    MarkExceptionsDirty();
}

void Core::AdvanceSysTick(uint32_t cycles) {
    if ((state_.syst_csr & 4u) == 0u) {
        state_.syst_prescale += cycles;
        cycles = state_.syst_prescale / 8u;
        state_.syst_prescale %= 8u;
    }
    while (cycles != 0u) {
        if (state_.syst_cvr == 0u) {
            state_.syst_cvr = state_.syst_rvr & 0x00FFFFFFu;
            --cycles;
            if (state_.syst_cvr == 0u) return;
            continue;
        }
        if (state_.syst_cvr > cycles) {
            state_.syst_cvr -= cycles;
            return;
        }
        cycles -= state_.syst_cvr;
        state_.syst_cvr = 0u;
        state_.syst_csr |= 1u << 16;
        if ((state_.syst_csr & 2u) != 0u) {
            SetPending(kExcSysTick);
            MarkExceptionsDirty();
        }
    }
}

}

}
