#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

uint32_t Core::IciRegister() const {
    return (state_.itstate & 0x0Fu) == 0u ? state_.itstate >> 4 : 0u;
}

void Core::CheckIciInstruction(Bus& bus) {
    if (IciRegister() == 0u) return;
    const uint16_t hw1 = Fetch16(bus, state_.pc);
    const bool multiple = (hw1 & 0xF000u) == 0xC000u || (hw1 & 0xF600u) == 0xB400u ||
                          ((hw1 & 0xFE40u) == 0xE800u && (((hw1 >> 7) & 3u) == 1u || ((hw1 >> 7) & 3u) == 2u));
    if (multiple) return;
    this_instr_ = state_.pc;
    cycles_ = 0u;
    TakeUsageFault(bus, kCfsrInvState);
}

void Core::TransferMultiple(Bus& bus, bool load, uint32_t n, bool increment, bool wback, uint32_t registers) {
    const uint32_t span = 4u * BitCount(registers);
    const bool base_in_list = load && (registers & (1u << n)) != 0u;
    const uint32_t ici = IciRegister();
    const uint32_t base_now = ReadReg(n);
    uint32_t lowest = increment ? base_now : base_now - span;
    if (ici != 0u && wback) lowest = (n == 13u || !increment) ? base_now : base_now - span;
    const uint32_t original = increment ? lowest : lowest + span;
    const uint32_t final_base = increment ? lowest + span : lowest;
    CheckWordAligned(bus, lowest);
    if (ici != 0u && ((registers & (1u << ici)) == 0u || (registers & ((1u << ici) - 1u)) == 0u))
        Unpredictable(bus, ici);

    uint32_t address = lowest;
    uint32_t pc_value = 0u;
    bool transferred = false;
    uint32_t arrival = ArrivalWithin();
    for (uint32_t i = 0u; i < 16u; ++i) {
        if ((registers & (1u << i)) == 0u) continue;
        if (i < ici) {
            address += 4u;
            continue;
        }
        if (transferred && arrival <= cycles_) {
            if (PendingAt(bus, cycles_)) return InterruptMultiple(n, i, wback, base_in_list, original, lowest, final_base);
            arrival = ArrivalWithin();
        }
        if (!load) {
            MemWrite(bus, address, 4u, ReadReg(i));
        } else if (i == 15u) {
            pc_value = MemRead(bus, address, 4u);
        } else {
            WriteReg(i, MemRead(bus, address, 4u));
        }
        transferred = true;
        address += 4u;
    }
    if (wback && !base_in_list) WriteReg(n, final_base);
    if (!InItBlock()) state_.itstate = 0u;
    if (load && (registers & 0x8000u) != 0u) LoadWritePc(bus, pc_value);
}

void Core::InterruptMultiple(uint32_t n, uint32_t next, bool wback, bool base_in_list, uint32_t original,
                             uint32_t lowest, uint32_t final_base) {
    state_.pc = this_instr_;
    branched_ = true;
    exception_event_ = true;
    if (InItBlock()) {
        if (base_in_list) WriteReg(n, original);
        return;
    }
    uint32_t ici = next;
    if (base_in_list && n < next) {
        WriteReg(n, original);
        ici = 0u;
    }
    if (wback) WriteReg(n, n == 13u ? lowest : final_base);
    state_.itstate = static_cast<uint8_t>(ici << 4);
}

}

}
