#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

namespace {

enum AluOp : uint8_t { kAdd, kSub, kRsb, kAnd, kBic, kOrr, kOrn, kEor, kMov, kMvn };

template <uint32_t op>
uint32_t Alu(uint32_t a, uint32_t b) {
    if constexpr (op == kAdd) return a + b;
    if constexpr (op == kSub) return a - b;
    if constexpr (op == kRsb) return b - a;
    if constexpr (op == kAnd) return a & b;
    if constexpr (op == kBic) return a & ~b;
    if constexpr (op == kOrr) return a | b;
    if constexpr (op == kOrn) return a | ~b;
    if constexpr (op == kEor) return a ^ b;
    if constexpr (op == kMov) return b;
    if constexpr (op == kMvn) return ~b;
}

bool DecodeAluOp(uint32_t op, uint32_t rn, uint8_t* alu) {
    switch (op) {
    case 0x0u: *alu = kAnd; return rn < 13u;
    case 0x1u: *alu = kBic; return rn < 13u;
    case 0x2u: *alu = rn == 15u ? kMov : kOrr; return rn < 13u || rn == 15u;
    case 0x3u: *alu = rn == 15u ? kMvn : kOrn; return rn < 13u || rn == 15u;
    case 0x4u: *alu = kEor; return rn < 13u;
    case 0x8u: *alu = kAdd; return rn < 13u;
    case 0xDu: *alu = kSub; return rn < 13u;
    case 0xEu: *alu = kRsb; return rn < 13u;
    default: return false;
    }
}

}

struct DecodedOps {
    static void Generic16(Core& c, Bus& bus, const Decoded& d) { c.Execute16(bus, d.hw1); }
    static void Generic32(Core& c, Bus& bus, const Decoded& d) { c.Execute32(bus, d.hw1, d.hw2); }

    static void MovRegister(Core& c, Bus&, const Decoded& d) { c.state_.r[d.d] = c.state_.r[d.m]; }
    static void MovImmediate(Core& c, Bus&, const Decoded& d) { c.state_.r[d.d] = d.imm; }
    static void MovTop(Core& c, Bus&, const Decoded& d) {
        c.state_.r[d.d] = (c.state_.r[d.d] & 0xFFFFu) | d.imm;
    }
    static void MovImmediateFlags(Core& c, Bus&, const Decoded& d) {
        c.state_.r[d.d] = d.imm;
        c.SetNzc(d.imm, c.FlagC());
    }

    static void AddSubFlags(Core& c, uint32_t rd, uint32_t a, uint32_t b, bool subtract) {
        bool carry = false;
        bool overflow = false;
        const uint32_t result = Core::AddWithCarry(a, subtract ? ~b : b, subtract, &carry, &overflow);
        c.state_.r[rd] = result;
        c.SetNzcv(result, carry, overflow);
    }
    static void AddRegisterFlags(Core& c, Bus&, const Decoded& d) {
        AddSubFlags(c, d.d, c.state_.r[d.n], c.state_.r[d.m], false);
    }
    static void SubRegisterFlags(Core& c, Bus&, const Decoded& d) {
        AddSubFlags(c, d.d, c.state_.r[d.n], c.state_.r[d.m], true);
    }
    static void AddImmediateFlags(Core& c, Bus&, const Decoded& d) {
        AddSubFlags(c, d.d, c.state_.r[d.n], d.imm, false);
    }
    static void SubImmediateFlags(Core& c, Bus&, const Decoded& d) {
        AddSubFlags(c, d.d, c.state_.r[d.n], d.imm, true);
    }
    static void Compare(Core& c, uint32_t a, uint32_t b) {
        bool carry = false;
        bool overflow = false;
        const uint32_t difference = Core::AddWithCarry(a, ~b, true, &carry, &overflow);
        c.SetNzcv(difference, carry, overflow);
    }
    static void CompareImmediate(Core& c, Bus&, const Decoded& d) { Compare(c, c.state_.r[d.n], d.imm); }
    static void CompareRegister(Core& c, Bus&, const Decoded& d) {
        Compare(c, c.state_.r[d.n], c.state_.r[d.m]);
    }

    template <uint32_t op>
    static void AluImmediate(Core& c, Bus&, const Decoded& d) {
        c.state_.r[d.d] = Alu<op>(c.state_.r[d.n], d.imm);
    }
    template <uint32_t op>
    static void AluShifted(Core& c, Bus&, const Decoded& d) {
        const ShiftResult shifted = Core::ShiftC(c.state_.r[d.m], static_cast<ShiftType>((d.imm >> 8) & 0xFFu),
                                                 d.imm >> 16, c.FlagC());
        c.state_.r[d.d] = Alu<op>(c.state_.r[d.n], shifted.value);
    }
    template <uint32_t op>
    static void AluShiftedLeft(Core& c, Bus&, const Decoded& d) {
        c.state_.r[d.d] = Alu<op>(c.state_.r[d.n], c.state_.r[d.m] << (d.imm >> 16));
    }

    using Handler = void (*)(Core&, Bus&, const Decoded&);
    template <uint32_t... ops>
    static Handler AluHandler(uint32_t op, uint32_t form) {
        static constexpr Handler kImmediate[] = {AluImmediate<ops>...};
        static constexpr Handler kShifted[] = {AluShifted<ops>...};
        static constexpr Handler kLeft[] = {AluShiftedLeft<ops>...};
        return form == 0u ? kImmediate[op] : form == 1u ? kShifted[op] : kLeft[op];
    }
    static Handler AluFor(uint32_t op, uint32_t form) {
        return AluHandler<kAdd, kSub, kRsb, kAnd, kBic, kOrr, kOrn, kEor, kMov, kMvn>(op, form);
    }

    static void Push(Core& c, Bus& bus, const Decoded& d) { c.TransferMultiple(bus, false, 13u, false, true, d.hw2); }
    static void Pop(Core& c, Bus& bus, const Decoded& d) { c.TransferMultiple(bus, true, 13u, true, true, d.hw2); }
    static void AdjustSp(Core& c, Bus&, const Decoded& d) { c.WriteReg(13u, c.ReadReg(13u) + d.imm); }
    static void Extend(Core& c, Bus&, const Decoded& d) { c.state_.r[d.d] = c.state_.r[d.m] & d.imm; }
    static void BranchExchange(Core& c, Bus& bus, const Decoded& d) { c.BxWritePc(bus, c.ReadReg(d.m)); }
    static void BranchLinkExchange(Core& c, Bus& bus, const Decoded& d) {
        const uint32_t target = c.ReadReg(d.m);
        c.state_.lr = (c.this_instr_ + 2u) | 1u;
        c.BlxWritePc(bus, target);
    }

    static void LoadWord(Core& c, Bus& bus, const Decoded& d) {
        c.state_.r[d.d] = c.MemRead(bus, c.ReadReg(d.n) + d.imm, 4u);
        c.PipelinedLoad(1u << d.n, d.d);
    }
    static void LoadHalf(Core& c, Bus& bus, const Decoded& d) {
        c.state_.r[d.d] = c.MemRead(bus, c.ReadReg(d.n) + d.imm, 2u);
        c.PipelinedLoad(1u << d.n, d.d);
    }
    static void LoadByte(Core& c, Bus& bus, const Decoded& d) {
        c.state_.r[d.d] = c.MemRead(bus, c.ReadReg(d.n) + d.imm, 1u);
        c.PipelinedLoad(1u << d.n, d.d);
    }
    static void LoadLiteral(Core& c, Bus& bus, const Decoded& d) {
        c.state_.r[d.d] = c.MemRead(bus, d.imm, 4u);
        c.PipelinedLoad(1u << 15, d.d);
    }

    static void StoreWord(Core& c, Bus& bus, const Decoded& d) {
        c.MemWrite(bus, c.ReadReg(d.n) + d.imm, 4u, c.state_.r[d.d]);
        --c.cycles_;
    }
    static void StoreHalf(Core& c, Bus& bus, const Decoded& d) {
        c.MemWrite(bus, c.ReadReg(d.n) + d.imm, 2u, c.state_.r[d.d] & 0xFFFFu);
        --c.cycles_;
    }
    static void StoreByte(Core& c, Bus& bus, const Decoded& d) {
        c.MemWrite(bus, c.ReadReg(d.n) + d.imm, 1u, c.state_.r[d.d] & 0xFFu);
        --c.cycles_;
    }

    static void Branch(Core& c, Bus&, const Decoded& d) { c.BranchWritePc(d.imm); }
    static void BranchConditional(Core& c, Bus&, const Decoded& d) {
        if (c.ConditionPassed(d.m)) c.BranchWritePc(d.imm);
    }
    static void BranchLink(Core& c, Bus&, const Decoded& d) {
        c.state_.lr = c.next_instr_ | 1u;
        c.BranchWritePc(d.imm);
    }
    static void CompareBranchZero(Core& c, Bus&, const Decoded& d) {
        if ((d.m != 0u) != (c.state_.r[d.n] == 0u)) c.BranchWritePc(d.imm);
    }

    static void Decode16(Decoded& d, uint32_t address, uint32_t hw);
    static void Decode32(Core& c, Decoded& d, uint32_t address, uint32_t hw1, uint32_t hw2);
};

void DecodedOps::Decode16(Decoded& d, uint32_t address, uint32_t hw) {
    const uint32_t pc = address + 4u;
    const uint32_t low_d = hw & 7u;
    const uint32_t low_n = (hw >> 3) & 7u;
    d.handler = Generic16;
    if ((hw & 0xFF00u) == 0x4600u) {
        d.d = static_cast<uint8_t>(((hw >> 4) & 8u) | low_d);
        d.m = static_cast<uint8_t>((hw >> 3) & 0xFu);
        if (d.d < 13u && d.m < 13u) d.handler = MovRegister;
    } else if ((hw & 0xF800u) == 0x2000u) {
        d.d = static_cast<uint8_t>((hw >> 8) & 7u);
        d.imm = hw & 0xFFu;
        d.handler = MovImmediateFlags;
    } else if ((hw & 0xF800u) == 0x2800u) {
        d.n = static_cast<uint8_t>((hw >> 8) & 7u);
        d.imm = hw & 0xFFu;
        d.handler = CompareImmediate;
    } else if ((hw & 0xF000u) == 0x3000u) {
        d.d = d.n = static_cast<uint8_t>((hw >> 8) & 7u);
        d.imm = hw & 0xFFu;
        d.handler = (hw & 0x0800u) != 0u ? SubImmediateFlags : AddImmediateFlags;
    } else if ((hw & 0xF800u) == 0x1800u) {
        d.d = static_cast<uint8_t>(low_d);
        d.n = static_cast<uint8_t>(low_n);
        d.m = static_cast<uint8_t>((hw >> 6) & 7u);
        d.imm = d.m;
        const bool subtract = (hw & 0x0200u) != 0u;
        if ((hw & 0x0400u) != 0u) {
            d.handler = subtract ? SubImmediateFlags : AddImmediateFlags;
        } else {
            d.handler = subtract ? SubRegisterFlags : AddRegisterFlags;
        }
    } else if ((hw & 0xFFC0u) == 0x4280u) {
        d.n = static_cast<uint8_t>(low_d);
        d.m = static_cast<uint8_t>(low_n);
        d.handler = CompareRegister;
    } else if ((hw & 0xFF00u) == 0x4500u) {
        d.n = static_cast<uint8_t>(((hw >> 4) & 8u) | low_d);
        d.m = static_cast<uint8_t>((hw >> 3) & 0xFu);
        if (d.n < 13u && d.m < 13u && (d.n >= 8u || d.m >= 8u)) d.handler = CompareRegister;
    } else if ((hw & 0xE000u) == 0x6000u || (hw & 0xF000u) == 0x8000u) {
        const bool load = (hw & 0x0800u) != 0u;
        const uint32_t imm5 = (hw >> 6) & 0x1Fu;
        d.d = static_cast<uint8_t>(low_d);
        d.n = static_cast<uint8_t>(low_n);
        if ((hw & 0xF000u) == 0x6000u) {
            d.imm = imm5 * 4u;
            d.handler = load ? LoadWord : StoreWord;
        } else if ((hw & 0xF000u) == 0x7000u) {
            d.imm = imm5;
            d.handler = load ? LoadByte : StoreByte;
        } else {
            d.imm = imm5 * 2u;
            d.handler = load ? LoadHalf : StoreHalf;
        }
    } else if ((hw & 0xF000u) == 0x9000u) {
        d.d = static_cast<uint8_t>((hw >> 8) & 7u);
        d.n = 13u;
        d.imm = (hw & 0xFFu) * 4u;
        d.handler = (hw & 0x0800u) != 0u ? LoadWord : StoreWord;
    } else if ((hw & 0xF800u) == 0x4800u) {
        d.d = static_cast<uint8_t>((hw >> 8) & 7u);
        d.imm = (pc & ~3u) + (hw & 0xFFu) * 4u;
        d.handler = LoadLiteral;
    } else if ((hw & 0xF000u) == 0xD000u && ((hw >> 8) & 0xFu) < 0xEu) {
        d.m = static_cast<uint8_t>((hw >> 8) & 0xFu);
        d.imm = pc + BranchOffsetT1(hw);
        d.handler = BranchConditional;
    } else if ((hw & 0xF800u) == 0xE000u) {
        d.imm = pc + BranchOffsetT2(hw);
        d.handler = Branch;
    } else if (((hw & 0xFE00u) == 0xB400u || (hw & 0xFE00u) == 0xBC00u) && (hw & 0x1FFu) != 0u) {
        const bool pop = (hw & 0x0800u) != 0u;
        const uint32_t registers = (hw & 0xFFu) | ((hw & 0x100u) << (pop ? 7 : 6));
        d.hw2 = static_cast<uint16_t>(registers);
        d.handler = pop ? Pop : Push;
    } else if ((hw & 0xFF00u) == 0xB000u) {
        const uint32_t imm = (hw & 0x7Fu) * 4u;
        d.imm = (hw & 0x80u) != 0u ? 0u - imm : imm;
        d.handler = AdjustSp;
    } else if ((hw & 0xFF80u) == 0xB280u) {
        d.d = static_cast<uint8_t>(low_d);
        d.m = static_cast<uint8_t>(low_n);
        d.imm = (hw & 0x40u) != 0u ? 0xFFu : 0xFFFFu;
        d.handler = Extend;
    } else if ((hw & 0xFF87u) == 0x4700u && ((hw >> 3) & 0xFu) != 15u) {
        d.m = static_cast<uint8_t>((hw >> 3) & 0xFu);
        d.handler = BranchExchange;
    } else if ((hw & 0xFF87u) == 0x4780u && ((hw >> 3) & 0xFu) != 15u) {
        d.m = static_cast<uint8_t>((hw >> 3) & 0xFu);
        d.handler = BranchLinkExchange;
    } else if ((hw & 0xF500u) == 0xB100u) {
        d.n = static_cast<uint8_t>(low_d);
        d.m = static_cast<uint8_t>((hw >> 11) & 1u);
        d.imm = pc + ((((hw >> 9) & 1u) << 6) | (((hw >> 3) & 0x1Fu) << 1));
        d.handler = CompareBranchZero;
    }
}

void DecodedOps::Decode32(Core& c, Decoded& d, uint32_t address, uint32_t hw1, uint32_t hw2) {
    const uint32_t pc = address + 4u;
    const uint32_t rn = hw1 & 0xFu;
    d.handler = Generic32;
    d.d = static_cast<uint8_t>((hw2 >> 8) & 0xFu);
    d.n = static_cast<uint8_t>(rn);
    const uint32_t imm12 = ((hw1 & 0x400u) << 1) | ((hw2 >> 4) & 0x700u) | (hw2 & 0xFFu);
    const bool data_imm = (hw1 & 0xF800u) == 0xF000u && (hw2 & 0x8000u) == 0u;
    if (data_imm && (hw1 & 0x0200u) != 0u) {
        const uint32_t op = (hw1 & 0xFBF0u);
        if (d.d >= 13u) return;
        if (op == 0xF240u) {
            d.imm = (rn << 12) | imm12;
            d.handler = MovImmediate;
        } else if (op == 0xF2C0u) {
            d.imm = ((rn << 12) | imm12) << 16;
            d.handler = MovTop;
        } else if ((op == 0xF200u || op == 0xF2A0u) && rn < 13u) {
            d.m = op == 0xF200u ? kAdd : kSub;
            d.imm = imm12;
            d.handler = AluFor(d.m, 0u);
        }
        return;
    }
    if (data_imm) {
        if ((hw1 & 0x10u) != 0u || d.d >= 13u || !DecodeAluOp((hw1 >> 5) & 0xFu, rn, &d.m)) return;
        if (Core::ThumbExpandImmUnpredictable(imm12)) return;
        if (rn == 15u) d.n = 0u;
        d.imm = c.ThumbExpandImmC(imm12).value;
        d.handler = AluFor(d.m, 0u);
        return;
    }
    if ((hw1 & 0xFE00u) == 0xEA00u) {
        const uint32_t rm = hw2 & 0xFu;
        uint8_t alu = 0u;
        if ((hw1 & 0x10u) != 0u || (hw2 & 0x8000u) != 0u || d.d >= 13u || rm >= 13u ||
            !DecodeAluOp((hw1 >> 5) & 0xFu, rn, &alu))
            return;
        ShiftType type = ShiftType::Lsl;
        uint32_t amount = 0u;
        if (rn == 15u) d.n = 0u;
        Core::DecodeImmShift((hw2 >> 4) & 3u, ((hw2 >> 10) & 0x1Cu) | ((hw2 >> 6) & 3u), &type, &amount);
        d.m = static_cast<uint8_t>(rm);
        d.imm = alu | (static_cast<uint32_t>(type) << 8) | (amount << 16);
        d.handler = AluFor(alu, type == ShiftType::Lsl ? 2u : 1u);
        return;
    }
    if ((hw1 & 0xF800u) == 0xF000u && (hw2 & 0x8000u) != 0u) {
        const uint32_t op1 = (hw2 >> 12) & 7u;
        if ((op1 & 5u) == 0u) {
            const uint32_t cond = (hw1 >> 6) & 0xFu;
            if (cond >= 0xEu) return;
            d.m = static_cast<uint8_t>(cond);
            d.imm = pc + BranchOffsetT3(hw1, hw2);
            d.handler = BranchConditional;
            return;
        }
        if ((op1 & 5u) != 1u && (op1 & 5u) != 5u) return;
        d.imm = pc + BranchOffsetT4(hw1, hw2);
        d.handler = (op1 & 4u) != 0u ? BranchLink : Branch;
        return;
    }
    if ((hw1 & 0xFF80u) == 0xF880u && rn != 15u) {
        const uint32_t rt = hw2 >> 12;
        const uint32_t size_field = (hw1 >> 5) & 3u;
        const bool load = (hw1 & 0x10u) != 0u;
        if (rt >= 13u || size_field == 3u) return;
        d.d = static_cast<uint8_t>(rt);
        d.imm = hw2 & 0xFFFu;
        if (size_field == 2u) {
            d.handler = load ? LoadWord : StoreWord;
        } else if (size_field == 1u) {
            d.handler = load ? LoadHalf : StoreHalf;
        } else {
            d.handler = load ? LoadByte : StoreByte;
        }
    }
}

void Core::Decode(Decoded& decoded, uint32_t address) {
    const uint8_t* code = code_host_ + (address - code_base_);
    uint16_t hw1 = 0u;
    std::memcpy(&hw1, code, 2u);
    decoded = Decoded{};
    if ((hw1 >> 11) < 0x1Du) {
        decoded.hw1 = hw1;
        decoded.size = 2u;
        DecodedOps::Decode16(decoded, address, hw1);
        return;
    }
    uint16_t hw2 = 0u;
    std::memcpy(&hw2, code + 2u, 2u);
    decoded.hw1 = hw1;
    decoded.hw2 = hw2;
    decoded.size = 4u;
    DecodedOps::Decode32(*this, decoded, address, hw1, hw2);
}

}

}
