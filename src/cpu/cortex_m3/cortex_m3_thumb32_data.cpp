#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

namespace {

uint32_t Magnitude(uint32_t value, bool is_signed) {
    return is_signed && static_cast<int32_t>(value) < 0 ? 0u - value : value;
}

int TopBit(uint32_t value) {
    int bit = -1;
    while (value != 0u) {
        ++bit;
        value >>= 1;
    }
    return bit;
}

uint32_t DivideCycles(uint32_t n, uint32_t m, bool is_signed) {
    const uint32_t dividend = Magnitude(n, is_signed);
    const bool negative_divisor = is_signed && static_cast<int32_t>(m) < 0;
    const uint32_t divisor = Magnitude(m, is_signed);
    if (n == 0u || m == 0u || divisor > dividend) return 2u;
    int diff = TopBit(dividend) - TopBit(divisor);
    if (is_signed) {
        const bool power_of_two = (divisor & (divisor - 1u)) == 0u;
        if (negative_divisor && power_of_two && dividend != 0x80000000u) ++diff;
        if (negative_divisor && !power_of_two && dividend == 0x80000000u) --diff;
    }
    return 5u + static_cast<uint32_t>(diff) / 4u;
}

uint32_t LongMultiplyCycles(uint32_t n, uint32_t m, bool is_signed, bool accumulates) {
    if (accumulates) {
        const uint32_t mag_n = Magnitude(n, is_signed);
        const uint32_t mag_m = Magnitude(m, is_signed);
        return (mag_n < 0x10000u && mag_m < 0x10000u ? 4u : 6u) + (is_signed ? 1u : 0u);
    }
    const bool n_hi = (n >> 16) != 0u, n_lo = (n & 0xFFFFu) != 0u, m_hi = (m >> 16) != 0u, m_lo = (m & 0xFFFFu) != 0u;
    const bool n_neg = static_cast<int32_t>(n) < 0, m_neg = static_cast<int32_t>(m) < 0;
    const bool same_half = (!n_hi && n_lo && !m_hi && m_lo) || (n_hi && !n_lo && m_hi && !m_lo);
    uint32_t cycles = 4u;
    if ((!n_hi && !n_lo) || (!m_hi && !m_lo)) {
        cycles = 3u;
    } else if (n_hi && n_lo && m_hi && m_lo) {
        cycles = 5u;
    } else if (!is_signed) {
        cycles = same_half ? 3u : 4u;
    } else if (same_half) {
        cycles = n_neg || m_neg ? 4u : 3u;
    } else if (n_hi && n_lo && m_hi && !m_lo) {
        cycles = n_neg || m_neg ? 5u : 4u;
    } else if (!(n_hi && !n_lo && m_hi && m_lo)) {
        cycles = m_neg ? 5u : 4u;
    }
    return cycles;
}

bool DataProcessingUnpredictable(uint32_t op, bool s, uint32_t d, uint32_t n, bool has_m, uint32_t m,
                                 ShiftType type, uint32_t amount) {
    const bool bad_m = has_m && Is13Or15(m);
    switch (op) {
    case 0x0u:
    case 0x4u:
        return d == 13u || (d == 15u && !s) || Is13Or15(n) || bad_m;
    case 0x1u:
        return Is13Or15(d) || Is13Or15(n) || bad_m;
    case 0x2u:
        if (has_m && n == 15u && !s && type == ShiftType::Lsl && amount == 0u)
            return d == 15u || m == 15u || (d == 13u && m == 13u);
        return Is13Or15(d) || n == 13u || bad_m;
    case 0x3u:
        return Is13Or15(d) || n == 13u || bad_m;
    case 0x8u:
    case 0xDu:
        if (d == 15u && s) return n == 15u || bad_m;
        if (n == 13u) return (d == 15u && !s) || bad_m || (has_m && d == 13u && (type != ShiftType::Lsl || amount > 3u));
        return d == 13u || (d == 15u && !s) || n == 15u || bad_m;
    case 0xAu:
    case 0xBu:
    case 0xEu:
        return Is13Or15(d) || Is13Or15(n) || bad_m;
    default:
        return false;
    }
}

}

void Core::ExecuteDataModifiedImm(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op = (hw1 >> 5) & 0xFu;
    const bool setflags = (hw1 & 0x10u) != 0u;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    const uint32_t imm12 = ((hw1 & 0x400u) << 1) | ((hw2 >> 4) & 0x700u) | (hw2 & 0xFFu);
    if (DataProcessingUnpredictable(op, setflags, rd, rn, false, 0u, ShiftType::Lsl, 0u) ||
        ThumbExpandImmUnpredictable(imm12))
        Unpredictable(bus, Encoding(hw1, hw2));
    ExecuteDataOperation(bus, hw1, hw2, ThumbExpandImmC(imm12));
}

void Core::ExecuteDataPlainImm(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op = (hw1 >> 4) & 0x1Fu;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    const uint32_t imm12 = ((hw1 & 0x400u) << 1) | ((hw2 >> 4) & 0x700u) | (hw2 & 0xFFu);
    const uint32_t lsbit = ((hw2 >> 10) & 0x1Cu) | ((hw2 >> 6) & 3u);
    const uint32_t field = hw2 & 0x1Fu;
    switch (op) {
    case 0x00u:
        if (rd == 15u || (rd == 13u && rn != 13u)) Unpredictable(bus, Encoding(hw1, hw2));
        WriteReg(rd, rn == 15u ? (ReadReg(15u) & ~3u) + imm12 : ReadReg(rn) + imm12);
        return;
    case 0x0Au:
        if (rd == 15u || (rd == 13u && rn != 13u)) Unpredictable(bus, Encoding(hw1, hw2));
        WriteReg(rd, rn == 15u ? (ReadReg(15u) & ~3u) - imm12 : ReadReg(rn) - imm12);
        return;
    case 0x04u:
        if (Is13Or15(rd)) Unpredictable(bus, Encoding(hw1, hw2));
        WriteReg(rd, (rn << 12) | imm12);
        return;
    case 0x0Cu:
        if (Is13Or15(rd)) Unpredictable(bus, Encoding(hw1, hw2));
        WriteReg(rd, (ReadReg(rd) & 0xFFFFu) | (((rn << 12) | imm12) << 16));
        return;
    case 0x10u: case 0x12u: case 0x18u: case 0x1Au: {
        const bool sh = (op & 2u) != 0u;
        if (sh && lsbit == 0u) Undefined(bus, Encoding(hw1, hw2));
        if ((hw1 & 0x400u) != 0u || (hw2 & 0x20u) != 0u || Is13Or15(rd) || Is13Or15(rn))
            Unpredictable(bus, Encoding(hw1, hw2));
        ShiftType type = ShiftType::Lsl;
        uint32_t amount = 0u;
        DecodeImmShift(sh ? 2u : 0u, lsbit, &type, &amount);
        const int64_t operand = static_cast<int32_t>(ShiftC(ReadReg(rn), type, amount, FlagC()).value);
        const int64_t high = (int64_t{1} << field) - 1;
        const int64_t low = (op & 8u) != 0u ? 0 : -(int64_t{1} << field);
        const int64_t result = operand > high ? high : operand < low ? low : operand;
        if (result != operand) state_.apsr |= kPsrQ;
        WriteReg(rd, static_cast<uint32_t>(result));
        return;
    }
    case 0x14u: case 0x1Cu: {
        const uint32_t msbit = lsbit + field;
        if ((hw1 & 0x400u) != 0u || (hw2 & 0x20u) != 0u || Is13Or15(rd) || Is13Or15(rn) || msbit > 31u)
            Unpredictable(bus, Encoding(hw1, hw2));
        const uint32_t width = field + 1u;
        const uint32_t value = width == 32u ? ReadReg(rn) : (ReadReg(rn) >> lsbit) & ((1u << width) - 1u);
        WriteReg(rd, op == 0x14u ? SignExtend(value, width) : value);
        return;
    }
    case 0x16u: {
        if ((hw1 & 0x400u) != 0u || (hw2 & 0x20u) != 0u || Is13Or15(rd) || rn == 13u || field < lsbit)
            Unpredictable(bus, Encoding(hw1, hw2));
        const uint32_t width = field - lsbit + 1u;
        const uint32_t mask = (width == 32u ? 0xFFFFFFFFu : ((1u << width) - 1u)) << lsbit;
        const uint32_t source = rn == 15u ? 0u : ReadReg(rn) << lsbit;
        WriteReg(rd, (ReadReg(rd) & ~mask) | (source & mask));
        return;
    }
    default: Undefined(bus, Encoding(hw1, hw2));
    }
}

void Core::ExecuteDataShiftedReg(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op = (hw1 >> 5) & 0xFu;
    const bool setflags = (hw1 & 0x10u) != 0u;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    ShiftType type = ShiftType::Lsl;
    uint32_t amount = 0u;
    if ((hw2 & 0x8000u) != 0u) Unpredictable(bus, Encoding(hw1, hw2));
    DecodeImmShift((hw2 >> 4) & 3u, ((hw2 >> 10) & 0x1Cu) | ((hw2 >> 6) & 3u), &type, &amount);
    if (DataProcessingUnpredictable(op, setflags, rd, rn, true, hw2 & 0xFu, type, amount))
        Unpredictable(bus, Encoding(hw1, hw2));
    ExecuteDataOperation(bus, hw1, hw2, ShiftC(ReadReg(hw2 & 0xFu), type, amount, FlagC()));
}

void Core::ExecuteDataOperation(Bus& bus, uint32_t hw1, uint32_t hw2, ShiftResult operand) {
    const uint32_t op = (hw1 >> 5) & 0xFu;
    const bool setflags = (hw1 & 0x10u) != 0u;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    const uint32_t a = ReadReg(rn);
    bool carry = false;
    bool overflow = false;
    switch (op) {
    case 0x0u: case 0x1u: case 0x2u: case 0x3u: case 0x4u: {
        uint32_t result = 0u;
        bool write = true;
        if (op == 0x0u) {
            result = a & operand.value;
            write = !(rd == 15u && setflags);
        } else if (op == 0x1u) {
            result = a & ~operand.value;
        } else if (op == 0x2u) {
            result = rn == 15u ? operand.value : a | operand.value;
        } else if (op == 0x3u) {
            result = rn == 15u ? ~operand.value : a | ~operand.value;
        } else {
            result = a ^ operand.value;
            write = !(rd == 15u && setflags);
        }
        if (write) WriteReg(rd, result);
        if (setflags) SetNzc(result, operand.carry);
        return;
    }
    case 0x8u: case 0xAu: case 0xBu: case 0xDu: case 0xEu: {
        uint32_t result = 0u;
        bool write = true;
        if (op == 0x8u) {
            result = AddWithCarry(a, operand.value, false, &carry, &overflow);
            write = !(rd == 15u && setflags);
        } else if (op == 0xAu) {
            result = AddWithCarry(a, operand.value, FlagC(), &carry, &overflow);
        } else if (op == 0xBu) {
            result = AddWithCarry(a, ~operand.value, FlagC(), &carry, &overflow);
        } else if (op == 0xDu) {
            result = AddWithCarry(a, ~operand.value, true, &carry, &overflow);
            write = !(rd == 15u && setflags);
        } else {
            result = AddWithCarry(~a, operand.value, true, &carry, &overflow);
        }
        if (write) WriteReg(rd, result);
        if (setflags) SetNzcv(result, carry, overflow);
        return;
    }
    default: Undefined(bus, Encoding(hw1, hw2));
    }
}

void Core::ExecuteDataRegister(Bus& bus, uint32_t hw1, uint32_t hw2) {
    if ((hw2 & 0xF000u) != 0xF000u) Undefined(bus, Encoding(hw1, hw2));
    const uint32_t op1 = (hw1 >> 4) & 0xFu;
    const uint32_t op2 = (hw2 >> 4) & 0xFu;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    const uint32_t m = ReadReg(hw2 & 0xFu);
    if ((op1 & 0x8u) == 0u && op2 == 0u) {
        if (Is13Or15(rd) || Is13Or15(rn) || Is13Or15(hw2 & 0xFu)) Unpredictable(bus, Encoding(hw1, hw2));
        static constexpr ShiftType kTypes[4] = {ShiftType::Lsl, ShiftType::Lsr, ShiftType::Asr, ShiftType::Ror};
        const ShiftResult r = ShiftC(ReadReg(rn), kTypes[(op1 >> 1) & 3u], m & 0xFFu, FlagC());
        WriteReg(rd, r.value);
        if ((op1 & 1u) != 0u) SetNzc(r.value, r.carry);
        return;
    }
    if ((op2 & 0x8u) != 0u && rn == 15u && (op1 == 0x0u || op1 == 0x1u || op1 == 0x4u || op1 == 0x5u)) {
        if ((op2 & 0x4u) != 0u || Is13Or15(rd) || Is13Or15(hw2 & 0xFu)) Unpredictable(bus, Encoding(hw1, hw2));
        const uint32_t rotated = ShiftC(m, ShiftType::Ror, (op2 & 3u) * 8u, false).value;
        switch (op1) {
        case 0x0u: WriteReg(rd, SignExtend(rotated & 0xFFFFu, 16u)); return;
        case 0x1u: WriteReg(rd, rotated & 0xFFFFu); return;
        case 0x4u: WriteReg(rd, SignExtend(rotated & 0xFFu, 8u)); return;
        case 0x5u: WriteReg(rd, rotated & 0xFFu); return;
        default: Undefined(bus, Encoding(hw1, hw2));
        }
    }
    if ((op1 & 0xCu) == 0x8u && (op2 & 0xCu) == 0x8u) {
        if (rn != (hw2 & 0xFu) || Is13Or15(rd) || Is13Or15(hw2 & 0xFu)) Unpredictable(bus, Encoding(hw1, hw2));
        switch (((op1 & 3u) << 2) | (op2 & 3u)) {
        case 0x4u: WriteReg(rd, ReverseBytes(m)); return;
        case 0x5u: WriteReg(rd, ReverseHalfwordBytes(m)); return;
        case 0x6u: {
            uint32_t result = 0u;
            for (uint32_t i = 0u; i < 32u; ++i) result |= ((m >> i) & 1u) << (31u - i);
            WriteReg(rd, result);
            return;
        }
        case 0x7u: WriteReg(rd, ReverseSignedHalfword(m)); return;
        case 0xCu: {
            uint32_t count = 0u;
            for (uint32_t bit = 0x80000000u; bit != 0u && (m & bit) == 0u; bit >>= 1) ++count;
            WriteReg(rd, count);
            return;
        }
        default: break;
        }
    }
    Undefined(bus, Encoding(hw1, hw2));
}

void Core::ExecuteMultiply(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op1 = (hw1 >> 4) & 7u;
    const uint32_t op2 = (hw2 >> 4) & 3u;
    if (op1 != 0u || (hw2 & 0xC0u) != 0u) Undefined(bus, Encoding(hw1, hw2));
    const uint32_t ra = hw2 >> 12;
    const uint32_t product = ReadReg(hw1 & 0xFu) * ReadReg(hw2 & 0xFu);
    const uint32_t rd = (hw2 >> 8) & 0xFu;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rm = hw2 & 0xFu;
    if (op2 == 0u) {
        if (Is13Or15(rd) || Is13Or15(rn) || Is13Or15(rm) || ra == 13u) Unpredictable(bus, Encoding(hw1, hw2));
        if (ra != 15u) ++cycles_;
        WriteReg(rd, ra == 15u ? product : ReadReg(ra) + product);
        return;
    }
    if (op2 == 1u) {
        if (Is13Or15(rd) || Is13Or15(rn) || Is13Or15(rm) || Is13Or15(ra)) Unpredictable(bus, Encoding(hw1, hw2));
        ++cycles_;
        WriteReg(rd, ReadReg(ra) - product);
        return;
    }
    Undefined(bus, Encoding(hw1, hw2));
}

void Core::ExecuteLongMultiplyDivide(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op1 = (hw1 >> 4) & 7u;
    const uint32_t op2 = (hw2 >> 4) & 0xFu;
    const uint32_t n = ReadReg(hw1 & 0xFu);
    const uint32_t m = ReadReg(hw2 & 0xFu);
    const uint32_t rdlo = hw2 >> 12;
    const uint32_t rdhi = (hw2 >> 8) & 0xFu;
    const uint32_t rn_field = hw1 & 0xFu;
    const uint32_t rm_field = hw2 & 0xFu;
    if ((op1 == 1u || op1 == 3u) && op2 == 0xFu) {
        if (rdlo != 0xFu || Is13Or15(rdhi) || Is13Or15(rn_field) || Is13Or15(rm_field))
            Unpredictable(bus, Encoding(hw1, hw2));
        if (InterruptedDuring(bus, DivideCycles(n, m, op1 == 1u))) return;
        if (m == 0u) {
            if ((state_.ccr & (1u << 4)) != 0u) TakeUsageFault(bus, kCfsrDivByZero);
            WriteReg(rdhi, 0u);
            return;
        }
        if (op1 == 3u) {
            WriteReg(rdhi, n / m);
            return;
        }
        const int64_t quotient = static_cast<int64_t>(static_cast<int32_t>(n)) / static_cast<int32_t>(m);
        WriteReg(rdhi, static_cast<uint32_t>(quotient));
        return;
    }
    if (op2 != 0u || (op1 & 1u) != 0u) Undefined(bus, Encoding(hw1, hw2));
    if (Is13Or15(rdlo) || Is13Or15(rdhi) || Is13Or15(rn_field) || Is13Or15(rm_field) || rdhi == rdlo)
        Unpredictable(bus, Encoding(hw1, hw2));
    const bool is_signed = op1 == 0u || op1 == 4u;
    const bool accumulates = op1 >= 4u;
    const uint32_t cost = LongMultiplyCycles(n, m, is_signed, accumulates);
    if (InterruptedDuring(bus, cost)) return;
    const uint64_t accumulate = (static_cast<uint64_t>(ReadReg(rdhi)) << 32) | ReadReg(rdlo);
    uint64_t result = is_signed ? static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(n)) * static_cast<int32_t>(m))
                       : static_cast<uint64_t>(n) * m;
    if (accumulates) result += accumulate;
    WriteReg(rdlo, static_cast<uint32_t>(result));
    WriteReg(rdhi, static_cast<uint32_t>(result >> 32));
}

}

}
