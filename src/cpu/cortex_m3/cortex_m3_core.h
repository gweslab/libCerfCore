#pragma once

#include "cortex_m3_state.h"

#include <cerfcore/core/force_inline.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

namespace cortex_m3 {

inline uint32_t BitCount(uint32_t value) {
    uint32_t count = 0u;
    for (; value != 0u; value &= value - 1u) ++count;
    return count;
}

inline uint32_t SignExtend(uint32_t value, uint32_t bits) {
    if (bits >= 32u) return value;
    const uint32_t sign = 1u << (bits - 1u);
    return ((value & ((sign << 1) - 1u)) ^ sign) - sign;
}

inline uint32_t ReverseBytes(uint32_t m) {
    return (m << 24) | ((m << 8) & 0xFF0000u) | ((m >> 8) & 0xFF00u) | (m >> 24);
}

inline uint32_t ReverseHalfwordBytes(uint32_t m) {
    return ((m << 8) & 0xFF00FF00u) | ((m >> 8) & 0x00FF00FFu);
}

inline uint32_t ReverseSignedHalfword(uint32_t m) {
    return SignExtend(((m << 8) & 0xFF00u) | ((m >> 8) & 0xFFu), 16u);
}

inline uint32_t BranchOffsetT1(uint32_t hw) {
    return SignExtend((hw & 0xFFu) << 1, 9u);
}

inline uint32_t BranchOffsetT2(uint32_t hw) {
    return SignExtend((hw & 0x7FFu) << 1, 12u);
}

inline uint32_t BranchOffsetT3(uint32_t hw1, uint32_t hw2) {
    const uint32_t s = (hw1 >> 10) & 1u, j1 = (hw2 >> 13) & 1u, j2 = (hw2 >> 11) & 1u;
    return SignExtend((s << 20) | (j2 << 19) | (j1 << 18) | ((hw1 & 0x3Fu) << 12) | ((hw2 & 0x7FFu) << 1), 21u);
}

inline uint32_t BranchOffsetT4(uint32_t hw1, uint32_t hw2) {
    const uint32_t s = (hw1 >> 10) & 1u;
    const uint32_t i1 = (~(((hw2 >> 13) & 1u) ^ s)) & 1u;
    const uint32_t i2 = (~(((hw2 >> 11) & 1u) ^ s)) & 1u;
    return SignExtend((s << 24) | (i1 << 23) | (i2 << 22) | ((hw1 & 0x3FFu) << 12) | ((hw2 & 0x7FFu) << 1), 25u);
}

inline uint32_t Encoding(uint32_t hw1, uint32_t hw2) {
    return (hw1 << 16) | hw2;
}

inline bool Is13Or15(uint32_t r) {
    return r == 13u || r == 15u;
}

class Bus {
public:
    virtual ~Bus() = default;
    virtual uint32_t Read(uint32_t address, uint32_t size) = 0;
    virtual void Write(uint32_t address, uint32_t size, uint32_t value) = 0;

    [[noreturn]] virtual void Fault(const char* what, uint32_t detail) = 0;
    virtual uint64_t Synchronize() = 0;
    virtual void SystemResetRequest() = 0;
    virtual bool EnterDeepSleep() = 0;
    virtual void ExitDeepSleep() = 0;
};

[[noreturn]] void Raise(Bus& bus, const char* what, uint32_t detail);

enum class ShiftType : uint8_t { Lsl, Lsr, Asr, Ror, Rrx };

struct ShiftResult {
    uint32_t value;
    bool carry;
};

struct SingleAddress {
    uint32_t address = 0u;
    uint32_t offset_addr = 0u;
    bool wback = false;
    bool register_form = false;
    bool unprivileged = false;
};

class Core;

struct Decoded {
    void (*handler)(Core&, Bus&, const Decoded&) = nullptr;
    uint32_t imm = 0u;
    uint16_t hw1 = 0u;
    uint16_t hw2 = 0u;
    uint8_t size = 0u;
    uint8_t d = 0u;
    uint8_t n = 0u;
    uint8_t m = 0u;
};

class Core {
public:
    void MapMemory(uint32_t base, uint32_t size, uint8_t* host, bool writable);
    void TrackWrites(uint32_t base, uint64_t* dirty_pages);
    static constexpr uint32_t kDirtyPageShift = 10u;

    void InvalidateCode(uint32_t address, uint32_t size);

    void Reset(Bus& bus, uint32_t vector_table);

    uint32_t Step(Bus& bus);

    void Run(Bus& bus, uint64_t cycle);
    void LowerRunLimit(uint64_t cycle) {
        if (cycle < run_limit_) run_limit_ = cycle;
    }
    uint64_t Cycles() const { return state_.cycles; }

    void SetExternalLevel(uint32_t irq, bool asserted);
    void SetInterruptLines(uint32_t lines);

    uint32_t ProgramCounter() const { return state_.pc; }
    uint32_t Register(uint32_t n) const { return n == 15u ? state_.pc : ReadReg(n); }
    uint32_t Ipsr() const { return state_.ipsr; }

    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

private:
    friend struct DecodedOps;

    uint32_t StepGeneric(Bus& bus);
    uint32_t StepDecoded(Bus& bus, Decoded& decoded);
    void Decode(Decoded& decoded, uint32_t address);

    uint32_t& Sp() {
        return (!state_.handler_mode && (state_.control & kControlSpSel) != 0u) ? state_.sp_process : state_.sp_main;
    }
    uint32_t ReadReg(uint32_t n) const {
        if (n < 13u) return state_.r[n];
        if (n == 13u)
            return (!state_.handler_mode && (state_.control & kControlSpSel) != 0u) ? state_.sp_process
                                                                                     : state_.sp_main;
        if (n == 14u) return state_.lr;
        return this_instr_ + 4u;
    }
    void WriteReg(uint32_t n, uint32_t value) {
        if (n < 13u) {
            state_.r[n] = value;
        } else if (n == 13u) {
            Sp() = value & 0xFFFFFFFCu;
        } else if (n == 14u) {
            state_.lr = value;
        } else {
            BranchWritePc(value);
        }
    }
    bool Privileged() const;

    bool ConditionPassed(uint32_t cond) const;
    uint32_t CurrentCondition() const;
    bool InItBlock() const { return (state_.itstate & 0x0Fu) != 0u; }
    bool LastInItBlock() const { return (state_.itstate & 0x0Fu) == 0x08u; }
    void ItAdvance();

    bool FlagC() const { return (state_.apsr & kPsrC) != 0u; }
    void SetNz(uint32_t result);
    void SetNzc(uint32_t result, bool carry);
    void SetNzcv(uint32_t result, bool carry, bool overflow);

    static uint32_t AddWithCarry(uint32_t x, uint32_t y, bool carry_in, bool* carry_out, bool* overflow);
    static ShiftResult ShiftC(uint32_t value, ShiftType type, uint32_t amount, bool carry_in);
    static void DecodeImmShift(uint32_t type, uint32_t imm5, ShiftType* shift_t, uint32_t* shift_n);
    ShiftResult ThumbExpandImmC(uint32_t imm12) const;
    static bool ThumbExpandImmUnpredictable(uint32_t imm12);

    void BranchWritePc(uint32_t address);
    void BxWritePc(Bus& bus, uint32_t address);
    void BlxWritePc(Bus& bus, uint32_t address);
    void LoadWritePc(Bus& bus, uint32_t address) {
        const bool exception_return = IsExceptionReturn(address);
        BxWritePc(bus, address);
        if (!exception_return) ++cycles_;
    }
    bool IsExceptionReturn(uint32_t address) const {
        return state_.handler_mode && (address & 0xF0000000u) == 0xF0000000u;
    }
    void AluWritePc(uint32_t address) {
        BranchWritePc(address);
        ++cycles_;
    }

    [[noreturn]] void UnalignedFault(Bus& bus);
    CERF_FORCEINLINE void CheckUnalignedTrap(Bus& bus, uint32_t address, uint32_t size) {
        if ((address & (size - 1u)) == 0u) return;
        if ((state_.ccr & kCcrUnalignTrp) != 0u) UnalignedFault(bus);
        cycles_ += (size == 4u && (address & 1u) != 0u) ? 2u : 1u;
    }
    struct SynchronousFault {};
    [[noreturn]] void TakeUsageFault(Bus& bus, uint32_t cfsr_bit);
    CERF_FORCEINLINE uint32_t MemRead(Bus& bus, uint32_t address, uint32_t size) {
        CheckUnalignedTrap(bus, address, size);
        ++cycles_;
        if (const uint8_t* p = Direct(address, size, false)) {
            if (size == 4u) {
                uint32_t value;
                std::memcpy(&value, p, 4u);
                return value;
            }
            if (size == 2u) {
                uint16_t value;
                std::memcpy(&value, p, 2u);
                return value;
            }
            return *p;
        }
        return MemReadDevice(bus, address, size);
    }
    CERF_FORCEINLINE void MemWrite(Bus& bus, uint32_t address, uint32_t size, uint32_t value) {
        CheckUnalignedTrap(bus, address, size);
        ++cycles_;
        if (uint8_t* p = Direct(address, size, true)) {
            if (size == 4u) {
                std::memcpy(p, &value, 4u);
            } else if (size == 2u) {
                const uint16_t half = static_cast<uint16_t>(value);
                std::memcpy(p, &half, 2u);
            } else {
                *p = static_cast<uint8_t>(value);
            }
            return;
        }
        MemWriteDevice(bus, address, size, value);
    }
    void CheckDeviceAlignment(Bus& bus, uint32_t address, uint32_t size);
    uint32_t MemReadDevice(Bus& bus, uint32_t address, uint32_t size);
    void MemWriteDevice(Bus& bus, uint32_t address, uint32_t size, uint32_t value);
    uint16_t Fetch16(Bus& bus, uint32_t address) {
        const DirectRegion& code = direct_[0];
        if (code.host != nullptr && address - code.base <= code.size - 2u) {
            uint16_t value;
            std::memcpy(&value, code.host + (address - code.base), 2u);
            return value;
        }
        return FetchSlow(bus, address);
    }
    uint16_t FetchSlow(Bus& bus, uint32_t address);
    uint8_t* Direct(uint32_t address, uint32_t size, bool write) const {
        for (uint32_t i = 0u; i < direct_count_; ++i) {
            const DirectRegion& r = direct_[i];
            if (address - r.base <= r.size - size) {
                if (write) {
                    if (!r.writable) return nullptr;
                    if (r.dirty != nullptr) MarkDirty(r.dirty, address - r.base, size);
                }
                return r.host + (address - r.base);
            }
        }
        return nullptr;
    }

    struct DirectRegion {
        uint32_t base = 0u;
        uint32_t size = 0u;
        uint8_t* host = nullptr;
        bool writable = false;
        uint64_t* dirty = nullptr;
    };
    static void MarkDirty(uint64_t* dirty, uint32_t offset, uint32_t size) {
        for (uint32_t page = offset >> kDirtyPageShift; page <= (offset + size - 1u) >> kDirtyPageShift; ++page)
            dirty[page >> 6] |= uint64_t{1} << (page & 63u);
    }
    DirectRegion direct_[2];
    uint32_t direct_count_ = 0u;
    uint32_t code_base_ = 0u;
    uint32_t code_size_ = 0u;
    uint32_t code_limit_ = 0u;
    const uint8_t* code_host_ = nullptr;
    std::vector<Decoded> code_;

    void Execute16(Bus& bus, uint16_t hw);
    void Execute16ShiftAddMove(Bus& bus, uint16_t hw);
    void Execute16DataProcessing(uint16_t hw);
    void Execute16SpecialBranch(Bus& bus, uint16_t hw);
    void Execute16LoadStore(Bus& bus, uint16_t hw);
    void Execute16Misc(Bus& bus, uint16_t hw);
    void Execute16Multiple(Bus& bus, uint16_t hw);
    void Execute32(Bus& bus, uint16_t hw1, uint16_t hw2);
    void ExecuteDataModifiedImm(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteDataPlainImm(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteDataShiftedReg(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteDataOperation(Bus& bus, uint32_t hw1, uint32_t hw2, ShiftResult operand);
    void ExecuteDataRegister(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteMultiply(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteLongMultiplyDivide(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteLoadStoreMultiple(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteDualExclusiveTable(Bus& bus, uint32_t hw1, uint32_t hw2);
    SingleAddress DecodeSingleAddress(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteLoadSingle(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteStoreSingle(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteBranchMisc(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteMrs(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteMsr(Bus& bus, uint32_t hw1, uint32_t hw2);
    void ExecuteCps(Bus& bus, uint16_t hw);
    void CheckWordAligned(Bus& bus, uint32_t address) {
        if ((address & 3u) != 0u) UnalignedFault(bus);
    }
    uint32_t IciRegister() const;
    void CheckIciInstruction(Bus& bus);
    void TransferMultiple(Bus& bus, bool load, uint32_t n, bool increment, bool wback, uint32_t registers);
    void InterruptMultiple(uint32_t n, uint32_t next, bool wback, bool base_in_list, uint32_t original, uint32_t lowest,
                           uint32_t final_base);
    [[noreturn]] void Undefined(Bus& bus, uint32_t encoding);
    [[noreturn]] void Coprocessor(Bus& bus, uint16_t hw1, uint16_t hw2);
    [[noreturn]] void Unpredictable(Bus& bus, uint32_t encoding);

    int ExecutionPriority(bool primask_applies = true) const;
    int BoostedPriority() const;
    static bool HasConfigurablePriority(uint32_t exception) {
        return exception >= kExcExternalBase || (exception >= 4u && exception <= 6u) || exception == 11u ||
               exception == 14u || exception == 15u;
    }
    int GroupPriority(uint32_t exception) const;
    int ExceptionPriority(uint32_t exception) const;
    uint32_t PendingToTake() {
        if (exceptions_dirty_) {
            pending_to_take_ = ComputePendingToTake();
            exceptions_dirty_ = false;
        }
        return pending_to_take_;
    }
    uint32_t ComputePendingToTake() const;
    bool IsPending(uint32_t exception) const;
    uint32_t HighestPending() const;
    void MarkExceptionsDirty() { exceptions_dirty_ = true; }
    void ExceptionEntry(Bus& bus, uint32_t exception);
    void ExceptionTaken(Bus& bus, uint32_t exception);
    uint32_t TakePendingException(Bus& bus, uint32_t exception);
    void ReturnUsageFault(Bus& bus, uint32_t exc_return);
    void CompleteReturn(Bus& bus, uint32_t frameptr, uint32_t exc_return);
    uint32_t ResumeSleepOnExitReturn(Bus& bus);
    void DeActivate(uint32_t returning);
    void ExceptionReturn(Bus& bus, uint32_t exc_return);
    uint32_t ImplementedExceptions() const { return kExcExternalBase + interrupt_lines_; }
    void PushStack(Bus& bus);
    void PopStack(Bus& bus, uint32_t frameptr, uint32_t exc_return);
    void TickSysTick(uint32_t cycles) {
        if ((state_.syst_csr & 1u) != 0u) AdvanceSysTick(cycles);
    }
    void AdvanceSysTick(uint32_t cycles);
    uint32_t ActiveCount() const;
    void SetPending(uint32_t exception);
    void NoteEnteredPending(bool was_pending, uint32_t exception);

    void CallSupervisor(Bus& bus);
    void BreakpointInstruction(Bus& bus);
    void EscalateToHardFault(Bus& bus, uint32_t hfsr_bits);

    void WaitForEvent(Bus& bus);
    void WaitForInterrupt(Bus& bus);
    void SendEvent() { state_.event_register = 1u; }
    bool WakeupPending(bool wfi) const;
    bool SleepEnds(Bus& bus);
    void EnterSleep(Bus& bus, uint8_t kind);
    uint32_t Idle();
    uint64_t SysTickCyclesToPending() const;

    void ClearExclusiveLocal() { state_.excl_active = 0u; }
    void ExecuteExclusive(Bus& bus, uint32_t hw1, uint32_t hw2);

    void PipelineAfterLoad(uint32_t address_registers) {
        if (prev_load_regs_ != 0u && (address_registers & prev_load_regs_) == 0u) --cycles_;
    }
    void PipelinedLoad(uint32_t address_registers, uint32_t dest) {
        PipelineAfterLoad(address_registers);
        if (dest != 15u) state_.load_regs = 1u << dest;
    }
    void PipelinedWritebackLoad(uint32_t address_registers, uint32_t dest, uint32_t base) {
        PipelineAfterLoad(address_registers);
        if (dest != 15u) state_.load_regs = (1u << dest) | (1u << base);
    }
    bool InterruptedDuring(Bus& bus, uint32_t cost);
    uint32_t ArrivalWithin() const;
    bool PendingAt(Bus& bus, uint32_t elapsed);
    uint32_t LateArrivals(Bus& bus, uint32_t current, uint32_t cost, uint32_t latency);
    bool TailChained(Bus& bus, uint32_t exc_return, uint32_t pop_cycles);
    void CommitCycles() {
        TickSysTick(cycles_ - committed_);
        state_.cycles += cycles_ - committed_;
    }

    struct UnprivilegedAccess {
        explicit UnprivilegedAccess(bool& flag) : flag_(flag) { flag_ = true; }
        ~UnprivilegedAccess() { flag_ = false; }
        bool& flag_;
    };
    uint32_t MemReadUnprivileged(Bus& bus, uint32_t address, uint32_t size) {
        const UnprivilegedAccess scope(unprivileged_access_);
        return MemRead(bus, address, size);
    }
    void MemWriteUnprivileged(Bus& bus, uint32_t address, uint32_t size, uint32_t value) {
        const UnprivilegedAccess scope(unprivileged_access_);
        MemWrite(bus, address, size, value);
    }

    static bool IsScs(uint32_t address);
    void CheckPpbPrivilege(Bus& bus, uint32_t address, bool write);
    uint32_t ScsRead(Bus& bus, uint32_t address, uint32_t size);
    void ScsWrite(Bus& bus, uint32_t address, uint32_t size, uint32_t value);

    State state_;
    uint64_t run_limit_ = 0u;
    uint64_t run_target_ = 0u;
    uint32_t committed_ = 0u;
    uint32_t this_instr_ = 0u;
    uint32_t interrupt_lines_ = 0u;
    uint32_t prev_load_regs_ = 0u;
    uint32_t next_instr_ = 0u;
    uint32_t cycles_ = 0u;
    bool branched_ = false;
    bool exception_event_ = false;
    bool unprivileged_access_ = false;
    bool exceptions_dirty_ = true;
    uint32_t pending_to_take_ = 0u;
};

}

}
