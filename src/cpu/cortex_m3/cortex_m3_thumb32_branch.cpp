#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

void Core::ExecuteBranchMisc(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op = (hw1 >> 4) & 0x7Fu;
    const uint32_t op1 = (hw2 >> 12) & 7u;

    if ((op1 & 5u) == 0u) {
        if ((op & 0x38u) != 0x38u) {
            if (InItBlock()) Unpredictable(bus, Encoding(hw1, hw2));
            const uint32_t cond = (hw1 >> 6) & 0xFu;
            if (ConditionPassed(cond)) BranchWritePc(ReadReg(15u) + BranchOffsetT3(hw1, hw2));
            return;
        }
        if ((op & 0x7Eu) == 0x38u) return ExecuteMsr(bus, hw1, hw2);
        if ((op & 0x7Eu) == 0x3Eu) return ExecuteMrs(bus, hw1, hw2);
        if (op == 0x3Au) {
            if (((hw2 >> 8) & 7u) != 0u) Undefined(bus, Encoding(hw1, hw2));
            const uint32_t hint = hw2 & 0xFFu;
            if (hint <= 4u || (hint & 0xF0u) == 0xF0u) {
                if ((hw1 & 0xFu) != 0xFu || (hw2 & 0x2800u) != 0u) Unpredictable(bus, Encoding(hw1, hw2));
                if (hint == 2u) WaitForEvent(bus);
                if (hint == 3u) WaitForInterrupt(bus);
                if (hint == 4u) SendEvent();
                return;
            }
            return;
        }
        if (op == 0x3Bu) {
            const uint32_t control = (hw2 >> 4) & 0xFu;
            if (control == 0x2u) {
                if ((hw1 & 0xFu) != 0xFu || (hw2 & 0x2F0Fu) != 0x0F0Fu) Unpredictable(bus, Encoding(hw1, hw2));
                ClearExclusiveLocal();
                return;
            }
            if (control == 0x4u || control == 0x5u || control == 0x6u) {
                if ((hw1 & 0xFu) != 0xFu || (hw2 & 0x2F00u) != 0x0F00u) Unpredictable(bus, Encoding(hw1, hw2));
                return;
            }
            Undefined(bus, Encoding(hw1, hw2));
        }
        Undefined(bus, Encoding(hw1, hw2));
    }
    if ((op1 & 5u) == 4u) Undefined(bus, Encoding(hw1, hw2));

    if (InItBlock() && !LastInItBlock()) Unpredictable(bus, Encoding(hw1, hw2));
    const uint32_t target = ReadReg(15u) + BranchOffsetT4(hw1, hw2);
    if ((op1 & 4u) != 0u) state_.lr = next_instr_ | 1u;
    BranchWritePc(target);
}

void Core::ExecuteMrs(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    const uint32_t sysm = hw2 & 0xFFu;
    const bool sysm_listed = sysm <= 3u || (sysm >= 5u && sysm <= 9u) || (sysm >= 16u && sysm <= 20u);
    if ((hw1 & 0x1Fu) != 0x0Fu || (hw2 & 0x2000u) != 0u || rd == 13u || rd == 15u || !sysm_listed)
        Unpredictable(bus, Encoding(hw1, hw2));
    uint32_t value = 0u;
    switch (sysm >> 3) {
    case 0u:
        if ((sysm & 1u) != 0u) value |= state_.ipsr & kIpsrMask;
        if ((sysm & 4u) == 0u) value |= state_.apsr & kApsrMask;
        break;
    case 1u:
        if (Privileged()) {
            if ((sysm & 7u) == 0u) value = state_.sp_main;
            if ((sysm & 7u) == 1u) value = state_.sp_process;
        }
        break;
    case 2u:
        switch (sysm & 7u) {
        case 0u: value = Privileged() ? state_.primask : 0u; break;
        case 1u: case 2u: value = Privileged() ? state_.basepri : 0u; break;
        case 3u: value = Privileged() ? state_.faultmask : 0u; break;
        case 4u: value = state_.control & 3u; break;
        default: Undefined(bus, Encoding(hw1, hw2));
        }
        break;
    default: Undefined(bus, Encoding(hw1, hw2));
    }
    WriteReg(rd, value);
}

void Core::ExecuteMsr(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t mask = (hw2 >> 10) & 3u;
    const uint32_t sysm = hw2 & 0xFFu;
    if ((hw1 & 0x10u) != 0u || (hw2 & 0x2300u) != 0u) Unpredictable(bus, Encoding(hw1, hw2));
    const bool sysm_listed = sysm <= 3u || (sysm >= 5u && sysm <= 9u) || (sysm >= 16u && sysm <= 20u);
    if (mask == 0u || (mask != 2u && sysm > 3u)) Unpredictable(bus, Encoding(hw1, hw2));
    if (rn == 13u || rn == 15u || !sysm_listed) Unpredictable(bus, Encoding(hw1, hw2));
    const uint32_t value = ReadReg(rn);
    MarkExceptionsDirty();
    switch (sysm >> 3) {
    case 0u:
        if ((sysm & 4u) == 0u) {
            if ((mask & 1u) != 0u) Unpredictable(bus, Encoding(hw1, hw2));
            if ((mask & 2u) != 0u) state_.apsr = value & kApsrMask;
        }
        return;
    case 1u:
        if (!Privileged()) return;
        if ((sysm & 7u) == 0u) state_.sp_main = value & 0xFFFFFFFCu;
        if ((sysm & 7u) == 1u) state_.sp_process = value & 0xFFFFFFFCu;
        return;
    case 2u:
        if (!Privileged()) return;
        switch (sysm & 7u) {
        case 0u: state_.primask = value & 1u; return;
        case 1u: state_.basepri = static_cast<uint8_t>(value & kPriorityMask); return;
        case 2u: {
            const uint32_t raw = value & 0xFFu;
            if (raw != 0u && (raw < state_.basepri || state_.basepri == 0u))
                state_.basepri = static_cast<uint8_t>(raw & kPriorityMask);
            return;
        }
        case 3u:
            if (ExecutionPriority() > -1) state_.faultmask = value & 1u;
            return;
        case 4u: {
            uint8_t control = static_cast<uint8_t>((state_.control & ~kControlNPriv) | (value & kControlNPriv));
            if (!state_.handler_mode)
                control = static_cast<uint8_t>((control & ~kControlSpSel) | (value & kControlSpSel));
            state_.control = control;
            return;
        }
        default: Undefined(bus, Encoding(hw1, hw2));
        }
    default: Undefined(bus, Encoding(hw1, hw2));
    }
}

}

}
