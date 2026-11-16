#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

void Core::ExecuteLoadStoreMultiple(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op = (hw1 >> 7) & 3u;
    const bool wback = (hw1 & 0x20u) != 0u;
    const bool load = (hw1 & 0x10u) != 0u;
    const uint32_t rn = hw1 & 0xFu;
    if (op != 1u && op != 2u) Undefined(bus, Encoding(hw1, hw2));
    const uint32_t zero_bits = load ? 0x2000u : 0xA000u;
    const uint32_t registers = hw2;
    if (rn == 15u || (hw2 & zero_bits) != 0u || BitCount(registers) < 2u || (wback && (registers & (1u << rn)) != 0u))
        Unpredictable(bus, Encoding(hw1, hw2));
    if (load && ((hw2 & 0xC000u) == 0xC000u || ((hw2 & 0x8000u) != 0u && InItBlock() && !LastInItBlock())))
        Unpredictable(bus, Encoding(hw1, hw2));
    TransferMultiple(bus, load, rn, op == 1u, wback, registers);
}

void Core::ExecuteDualExclusiveTable(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op1 = (hw1 >> 7) & 3u;
    const uint32_t op2 = (hw1 >> 4) & 3u;
    const uint32_t op3 = (hw2 >> 4) & 0xFu;
    const uint32_t rn = hw1 & 0xFu;
    if (op1 == 1u && op2 == 1u && (op3 == 0u || op3 == 1u)) {
        const uint32_t rm = hw2 & 0xFu;
        if ((hw2 & 0xFF00u) != 0xF000u || rn == 13u || rm == 13u || rm == 15u || (InItBlock() && !LastInItBlock()))
            Unpredictable(bus, Encoding(hw1, hw2));
        const uint32_t m = ReadReg(rm);
        const uint32_t halfwords = op3 == 0u ? MemRead(bus, ReadReg(rn) + m, 1u)
                                             : MemRead(bus, ReadReg(rn) + (m << 1), 2u);
        BranchWritePc(ReadReg(15u) + 2u * halfwords);
        cycles_ += 3u;
        return;
    }
    if (!((op1 & 2u) != 0u || (op2 & 2u) != 0u)) return ExecuteExclusive(bus, hw1, hw2);
    const bool index = (hw1 & 0x100u) != 0u;
    const bool add = (hw1 & 0x80u) != 0u;
    const bool wback = (hw1 & 0x20u) != 0u;
    const bool load = (hw1 & 0x10u) != 0u;
    const uint32_t rt = hw2 >> 12;
    const uint32_t rt2 = (hw2 >> 8) & 0xFu;
    const uint32_t imm32 = (hw2 & 0xFFu) * 4u;
    if (rt == 13u || rt == 15u || rt2 == 13u || rt2 == 15u || (wback && (rn == rt || rn == rt2)) ||
        (load && (rt == rt2 || (rn == 15u && wback))) || (!load && rn == 15u))
        Unpredictable(bus, Encoding(hw1, hw2));
    const uint32_t base = rn == 15u ? (ReadReg(15u) & ~3u) : ReadReg(rn);
    const uint32_t offset_addr = add ? base + imm32 : base - imm32;
    const uint32_t address = index ? offset_addr : base;
    CheckWordAligned(bus, address);
    if (load) {
        const uint32_t first = MemRead(bus, address, 4u);
        const uint32_t second = MemRead(bus, address + 4u, 4u);
        WriteReg(rt, first);
        WriteReg(rt2, second);
    } else {
        MemWrite(bus, address, 4u, ReadReg(rt));
        MemWrite(bus, address + 4u, 4u, ReadReg(rt2));
    }
    if (wback) WriteReg(rn, offset_addr);
}

SingleAddress Core::DecodeSingleAddress(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rt = hw2 >> 12;
    SingleAddress a;
    if ((hw1 & 0x80u) != 0u) {
        a.address = ReadReg(rn) + (hw2 & 0xFFFu);
    } else if ((hw2 & 0x0FC0u) == 0u) {
        a.register_form = true;
        a.address = ReadReg(rn) + (ReadReg(hw2 & 0xFu) << ((hw2 >> 4) & 3u));
    } else if ((hw2 & 0x0800u) != 0u) {
        const bool index = (hw2 & 0x400u) != 0u;
        const bool add = (hw2 & 0x200u) != 0u;
        a.wback = (hw2 & 0x100u) != 0u;
        if (index && add && !a.wback) {
            if (Is13Or15(rt)) Unpredictable(bus, Encoding(hw1, hw2));
            a.unprivileged = true;
        }
        if (!index && !a.wback) Undefined(bus, Encoding(hw1, hw2));
        const uint32_t imm8 = hw2 & 0xFFu;
        a.offset_addr = add ? ReadReg(rn) + imm8 : ReadReg(rn) - imm8;
        a.address = index ? a.offset_addr : ReadReg(rn);
    } else {
        Undefined(bus, Encoding(hw1, hw2));
    }
    return a;
}

void Core::ExecuteLoadSingle(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const bool sign = (hw1 & 0x100u) != 0u;
    const bool imm12_form = (hw1 & 0x80u) != 0u;
    const uint32_t size = 1u << ((hw1 >> 5) & 3u);
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rt = hw2 >> 12;
    if (size == 4u && sign) Undefined(bus, Encoding(hw1, hw2));

    SingleAddress a;
    if (rn == 15u) {
        const uint32_t imm12 = hw2 & 0xFFFu;
        const uint32_t base = ReadReg(15u) & ~3u;
        a.address = imm12_form ? base + imm12 : base - imm12;
    } else {
        a = DecodeSingleAddress(bus, hw1, hw2);
    }

    const uint32_t rm = hw2 & 0xFu;
    if (size == 4u) {
        if ((rt == 15u && InItBlock() && !LastInItBlock()) || (a.wback && rn == rt) ||
            (a.register_form && Is13Or15(rm)))
            Unpredictable(bus, Encoding(hw1, hw2));
    } else {
        if (rt == 13u || (rt == 15u && a.wback) || (a.wback && rn == rt) ||
            (a.register_form && Is13Or15(rm) && (rt != 15u || size == 1u)))
            Unpredictable(bus, Encoding(hw1, hw2));
        if (rt == 15u) return;
    }
    uint32_t value = a.unprivileged ? MemReadUnprivileged(bus, a.address, size) : MemRead(bus, a.address, size);
    if (sign) value = SignExtend(value, size * 8u);
    if (rt == 15u && (a.address & 3u) != 0u) Unpredictable(bus, Encoding(hw1, hw2));
    if (a.wback) WriteReg(rn, a.offset_addr);
    if (rt == 15u) {
        LoadWritePc(bus, value);
    } else {
        WriteReg(rt, value);
    }
    if (a.wback) {
        PipelinedWritebackLoad(1u << rn, rt, rn);
    } else {
        PipelinedLoad((1u << rn) | (a.register_form ? 1u << rm : 0u), rt);
    }
}

void Core::ExecuteStoreSingle(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const bool imm12_form = (hw1 & 0x80u) != 0u;
    const uint32_t size_field = (hw1 >> 5) & 3u;
    const uint32_t rn = hw1 & 0xFu;
    const uint32_t rt = hw2 >> 12;
    if (size_field == 3u || rn == 15u) Undefined(bus, Encoding(hw1, hw2));
    const uint32_t size = 1u << size_field;

    const SingleAddress a = DecodeSingleAddress(bus, hw1, hw2);
    if (rt == 15u || (size != 4u && rt == 13u) || (a.wback && rn == rt) ||
        (a.register_form && Is13Or15(hw2 & 0xFu)))
        Unpredictable(bus, Encoding(hw1, hw2));
    const uint32_t mask = size == 4u ? 0xFFFFFFFFu : (1u << (size * 8u)) - 1u;
    if (a.unprivileged) {
        MemWriteUnprivileged(bus, a.address, size, ReadReg(rt) & mask);
    } else {
        MemWrite(bus, a.address, size, ReadReg(rt) & mask);
    }
    if (imm12_form || (hw2 & 0x0800u) != 0u) --cycles_;
    if (a.register_form) PipelineAfterLoad((1u << rn) | (1u << (hw2 & 0xFu)));
    if (a.wback) WriteReg(rn, a.offset_addr);
}

}

}
