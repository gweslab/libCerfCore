#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

void Core::Execute16(Bus& bus, uint16_t hw) {
    const uint32_t opcode = hw >> 10;
    if ((opcode & 0x30u) == 0x00u) return Execute16ShiftAddMove(bus, hw);
    if (opcode == 0x10u) return Execute16DataProcessing(hw);
    if (opcode == 0x11u) return Execute16SpecialBranch(bus, hw);
    if ((opcode & 0x3Eu) == 0x12u) {
        const uint32_t address = (ReadReg(15u) & ~3u) + (hw & 0xFFu) * 4u;
        WriteReg((hw >> 8) & 7u, MemRead(bus, address, 4u));
        PipelinedLoad(1u << 15, (hw >> 8) & 7u);
        return;
    }
    if ((opcode & 0x3Cu) == 0x14u || (opcode & 0x38u) == 0x18u || (opcode & 0x38u) == 0x20u)
        return Execute16LoadStore(bus, hw);
    if ((opcode & 0x3Eu) == 0x28u) {
        WriteReg((hw >> 8) & 7u, (ReadReg(15u) & ~3u) + (hw & 0xFFu) * 4u);
        return;
    }
    if ((opcode & 0x3Eu) == 0x2Au) {
        WriteReg((hw >> 8) & 7u, ReadReg(13u) + (hw & 0xFFu) * 4u);
        return;
    }
    if ((opcode & 0x3Cu) == 0x2Cu) return Execute16Misc(bus, hw);
    if ((opcode & 0x3Cu) == 0x30u) return Execute16Multiple(bus, hw);
    if ((opcode & 0x3Cu) == 0x34u) {
        const uint32_t cond = (hw >> 8) & 0xFu;
        if (cond == 0xEu) Undefined(bus, hw);
        if (cond == 0xFu) return CallSupervisor(bus);
        if (InItBlock()) Unpredictable(bus, hw);
        if (ConditionPassed(cond)) BranchWritePc(ReadReg(15u) + BranchOffsetT1(hw));
        return;
    }
    if ((opcode & 0x3Eu) == 0x38u) {
        if (InItBlock() && !LastInItBlock()) Unpredictable(bus, hw);
        BranchWritePc(ReadReg(15u) + BranchOffsetT2(hw));
        return;
    }
    Undefined(bus, hw);
}

void Core::Execute16ShiftAddMove(Bus& bus, uint16_t hw) {
    const uint32_t opcode = (hw >> 9) & 0x1Fu;
    const bool setflags = !InItBlock();
    const uint32_t rd = hw & 7u;
    const uint32_t rn = (hw >> 3) & 7u;
    bool carry = false;
    bool overflow = false;
    if (opcode < 0x0Cu) {
        if (opcode < 4u && ((hw >> 6) & 0x1Fu) == 0u && InItBlock()) Unpredictable(bus, hw);
        ShiftType type = ShiftType::Lsl;
        uint32_t amount = 0u;
        DecodeImmShift(opcode >> 2, (hw >> 6) & 0x1Fu, &type, &amount);
        const ShiftResult r = ShiftC(ReadReg(rn), type, amount, FlagC());
        WriteReg(rd, r.value);
        if (setflags) SetNzc(r.value, r.carry);
        return;
    }
    const uint32_t rdn = (hw >> 8) & 7u;
    const uint32_t imm8 = hw & 0xFFu;
    switch (opcode) {
    case 0x0Cu: case 0x0Du: case 0x0Eu: case 0x0Fu: {
        const uint32_t operand = (opcode & 2u) != 0u ? (hw >> 6) & 7u : ReadReg((hw >> 6) & 7u);
        const bool subtract = (opcode & 1u) != 0u;
        const uint32_t result =
            AddWithCarry(ReadReg(rn), subtract ? ~operand : operand, subtract, &carry, &overflow);
        WriteReg(rd, result);
        if (setflags) SetNzcv(result, carry, overflow);
        return;
    }
    case 0x10u: case 0x11u: case 0x12u: case 0x13u:
        WriteReg(rdn, imm8);
        if (setflags) SetNzc(imm8, FlagC());
        return;
    case 0x14u: case 0x15u: case 0x16u: case 0x17u: {
        const uint32_t result = AddWithCarry(ReadReg(rdn), ~imm8, true, &carry, &overflow);
        SetNzcv(result, carry, overflow);
        return;
    }
    default: {
        const bool subtract = opcode >= 0x1Cu;
        const uint32_t result =
            AddWithCarry(ReadReg(rdn), subtract ? ~imm8 : imm8, subtract, &carry, &overflow);
        WriteReg(rdn, result);
        if (setflags) SetNzcv(result, carry, overflow);
        return;
    }
    }
}

void Core::Execute16DataProcessing(uint16_t hw) {
    const uint32_t opcode = (hw >> 6) & 0xFu;
    const uint32_t rdn = hw & 7u;
    const uint32_t rm = (hw >> 3) & 7u;
    const bool setflags = !InItBlock();
    const uint32_t a = ReadReg(rdn);
    const uint32_t b = ReadReg(rm);
    bool carry = FlagC();
    bool overflow = false;
    uint32_t result = 0u;
    switch (opcode) {
    case 0x0u: result = a & b; break;
    case 0x1u: result = a ^ b; break;
    case 0x2u: case 0x3u: case 0x4u: case 0x7u: {
        const ShiftType type = opcode == 0x2u ? ShiftType::Lsl
                             : opcode == 0x3u ? ShiftType::Lsr
                             : opcode == 0x4u ? ShiftType::Asr : ShiftType::Ror;
        const ShiftResult r = ShiftC(a, type, b & 0xFFu, FlagC());
        result = r.value;
        carry = r.carry;
        break;
    }
    case 0x5u: result = AddWithCarry(a, b, FlagC(), &carry, &overflow); break;
    case 0x6u: result = AddWithCarry(a, ~b, FlagC(), &carry, &overflow); break;
    case 0x8u: SetNzc(a & b, FlagC()); return;
    case 0x9u:
        result = AddWithCarry(~b, 0u, true, &carry, &overflow);
        WriteReg(rdn, result);
        if (setflags) SetNzcv(result, carry, overflow);
        return;
    case 0xAu: {
        const uint32_t difference = AddWithCarry(a, ~b, true, &carry, &overflow);
        SetNzcv(difference, carry, overflow);
        return;
    }
    case 0xBu: {
        const uint32_t sum = AddWithCarry(a, b, false, &carry, &overflow);
        SetNzcv(sum, carry, overflow);
        return;
    }
    case 0xCu: result = a | b; break;
    case 0xDu:
        result = a * b;
        WriteReg(rdn, result);
        if (setflags) SetNz(result);
        return;
    case 0xEu: result = a & ~b; break;
    default: result = ~b; break;
    }
    WriteReg(rdn, result);
    if (!setflags) return;
    if (opcode == 0x5u || opcode == 0x6u) {
        SetNzcv(result, carry, overflow);
    } else {
        SetNzc(result, carry);
    }
}

void Core::Execute16SpecialBranch(Bus& bus, uint16_t hw) {
    const uint32_t opcode = (hw >> 6) & 0xFu;
    const uint32_t rm = (hw >> 3) & 0xFu;
    const uint32_t rdn = ((hw >> 4) & 8u) | (hw & 7u);
    if (opcode < 0x4u) {
        if (rdn == 15u && ((InItBlock() && !LastInItBlock()) || rm == 15u)) Unpredictable(bus, hw);
        const uint32_t result = ReadReg(rdn) + ReadReg(rm);
        if (rdn == 15u) {
            AluWritePc(result);
        } else {
            WriteReg(rdn, result);
        }
        return;
    }
    if (opcode < 0x8u) {
        if (opcode == 0x4u || rdn == 15u || rm == 15u) Unpredictable(bus, hw);
        bool carry = false;
        bool overflow = false;
        const uint32_t difference = AddWithCarry(ReadReg(rdn), ~ReadReg(rm), true, &carry, &overflow);
        SetNzcv(difference, carry, overflow);
        return;
    }
    if (opcode < 0xCu) {
        if (rdn == 15u && InItBlock() && !LastInItBlock()) Unpredictable(bus, hw);
        if (rdn == 15u) {
            AluWritePc(ReadReg(rm));
        } else {
            WriteReg(rdn, ReadReg(rm));
        }
        return;
    }
    if (opcode < 0xEu) {
        if ((hw & 7u) != 0u || (InItBlock() && !LastInItBlock())) Unpredictable(bus, hw);
        BxWritePc(bus, ReadReg(rm));
        return;
    }
    if ((hw & 7u) != 0u || rm == 15u || (InItBlock() && !LastInItBlock())) Unpredictable(bus, hw);
    const uint32_t target = ReadReg(rm);
    state_.lr = (this_instr_ + 2u) | 1u;
    BlxWritePc(bus, target);
}

void Core::Execute16LoadStore(Bus& bus, uint16_t hw) {
    const uint32_t op_a = hw >> 12;
    const uint32_t op_b = (hw >> 9) & 7u;
    const uint32_t rt = hw & 7u;
    const uint32_t rn = (hw >> 3) & 7u;
    if (op_a == 0x5u) {
        const uint32_t address = ReadReg(rn) + ReadReg((hw >> 6) & 7u);
        const uint32_t address_registers = (1u << rn) | (1u << ((hw >> 6) & 7u));
        if (op_b < 3u) {
            PipelineAfterLoad(address_registers);
        } else {
            PipelinedLoad(address_registers, rt);
        }
        switch (op_b) {
        case 0u: MemWrite(bus, address, 4u, ReadReg(rt)); return;
        case 1u: MemWrite(bus, address, 2u, ReadReg(rt) & 0xFFFFu); return;
        case 2u: MemWrite(bus, address, 1u, ReadReg(rt) & 0xFFu); return;
        case 3u: WriteReg(rt, SignExtend(MemRead(bus, address, 1u), 8u)); return;
        case 4u: WriteReg(rt, MemRead(bus, address, 4u)); return;
        case 5u: WriteReg(rt, MemRead(bus, address, 2u)); return;
        case 6u: WriteReg(rt, MemRead(bus, address, 1u)); return;
        default: WriteReg(rt, SignExtend(MemRead(bus, address, 2u), 16u)); return;
        }
    }
    const bool load = (op_b & 4u) != 0u;
    const uint32_t imm5 = (hw >> 6) & 0x1Fu;
    if (op_a == 0x9u) {
        const uint32_t sp_rt = (hw >> 8) & 7u;
        const uint32_t address = ReadReg(13u) + (hw & 0xFFu) * 4u;
        if (load) {
            WriteReg(sp_rt, MemRead(bus, address, 4u));
            PipelinedLoad(1u << 13, sp_rt);
        } else {
            MemWrite(bus, address, 4u, ReadReg(sp_rt));
            --cycles_;
        }
        return;
    }
    const uint32_t size = op_a == 0x6u ? 4u : op_a == 0x7u ? 1u : 2u;
    const uint32_t address = ReadReg(rn) + imm5 * size;
    if (load) {
        WriteReg(rt, MemRead(bus, address, size));
        PipelinedLoad(1u << rn, rt);
    } else {
        const uint32_t mask = size == 4u ? 0xFFFFFFFFu : size == 2u ? 0xFFFFu : 0xFFu;
        MemWrite(bus, address, size, ReadReg(rt) & mask);
        --cycles_;
    }
}

void Core::Execute16Misc(Bus& bus, uint16_t hw) {
    const uint32_t opcode = (hw >> 5) & 0x7Fu;
    if (opcode == 0x33u) return ExecuteCps(bus, hw);
    if ((opcode & 0x7Cu) == 0x00u) {
        WriteReg(13u, ReadReg(13u) + (hw & 0x7Fu) * 4u);
        return;
    }
    if ((opcode & 0x7Cu) == 0x04u) {
        WriteReg(13u, ReadReg(13u) - (hw & 0x7Fu) * 4u);
        return;
    }
    if ((opcode & 0x28u) == 0x08u) {
        if (InItBlock()) Unpredictable(bus, hw);
        const bool nonzero = (hw & 0x0800u) != 0u;
        const uint32_t imm32 = (((hw >> 9) & 1u) << 6) | (((hw >> 3) & 0x1Fu) << 1);
        if (nonzero != (ReadReg(hw & 7u) == 0u)) BranchWritePc(ReadReg(15u) + imm32);
        return;
    }
    const uint32_t rd = hw & 7u;
    const uint32_t m = ReadReg((hw >> 3) & 7u);
    switch (opcode & 0x7Eu) {
    case 0x10u: WriteReg(rd, SignExtend(m & 0xFFFFu, 16u)); return;
    case 0x12u: WriteReg(rd, SignExtend(m & 0xFFu, 8u)); return;
    case 0x14u: WriteReg(rd, m & 0xFFFFu); return;
    case 0x16u: WriteReg(rd, m & 0xFFu); return;
    case 0x50u: WriteReg(rd, ReverseBytes(m)); return;
    case 0x52u: WriteReg(rd, ReverseHalfwordBytes(m)); return;
    case 0x56u: WriteReg(rd, ReverseSignedHalfword(m)); return;
    default: break;
    }
    if ((opcode & 0x70u) == 0x20u) {
        const uint32_t registers = (hw & 0xFFu) | ((hw & 0x100u) << 6);
        if (registers == 0u) Unpredictable(bus, hw);
        TransferMultiple(bus, false, 13u, false, true, registers);
        return;
    }
    if ((opcode & 0x70u) == 0x60u) {
        const uint32_t registers = (hw & 0xFFu) | ((hw & 0x100u) << 7);
        if (registers == 0u || ((registers & 0x8000u) != 0u && InItBlock() && !LastInItBlock()))
            Unpredictable(bus, hw);
        TransferMultiple(bus, true, 13u, true, true, registers);
        return;
    }
    if ((opcode & 0x78u) == 0x70u) return BreakpointInstruction(bus);
    if ((opcode & 0x78u) == 0x78u) {
        if ((hw & 0xFu) != 0u) {
            const uint32_t firstcond = (hw >> 4) & 0xFu;
            if (firstcond == 0xFu || (firstcond == 0xEu && BitCount(hw & 0xFu) != 1u) || InItBlock())
                Unpredictable(bus, hw);
            state_.itstate = static_cast<uint8_t>(hw & 0xFFu);
            return;
        }
        const uint32_t hint = (hw >> 4) & 0xFu;
        if (hint == 0u || hint == 1u) return;
        if (hint == 2u) return WaitForEvent(bus);
        if (hint == 3u) return WaitForInterrupt(bus);
        if (hint == 4u) return SendEvent();
        return;
    }
    Undefined(bus, hw);
}

void Core::Execute16Multiple(Bus& bus, uint16_t hw) {
    const uint32_t rn = (hw >> 8) & 7u;
    const uint32_t registers = hw & 0xFFu;
    if (registers == 0u) Unpredictable(bus, hw);
    const bool load = (hw & 0x0800u) != 0u;
    TransferMultiple(bus, load, rn, true, !load || (registers & (1u << rn)) == 0u, registers);
}

void Core::ExecuteCps(Bus& bus, uint16_t hw) {
    if ((hw & 0xCu) != 0u || (hw & 3u) == 0u || InItBlock()) Unpredictable(bus, hw);
    if (!Privileged()) return;
    MarkExceptionsDirty();
    const bool disable = (hw & 0x10u) != 0u;
    if ((hw & 2u) != 0u) state_.primask = disable ? 1u : 0u;
    if ((hw & 1u) != 0u) {
        if (!disable) {
            state_.faultmask = 0u;
        } else if (ExecutionPriority() > -1) {
            state_.faultmask = 1u;
        }
    }
}

}

}
