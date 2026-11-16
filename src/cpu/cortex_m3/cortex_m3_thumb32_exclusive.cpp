#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

void Core::ExecuteExclusive(Bus& bus, uint32_t hw1, uint32_t hw2) {
    const uint32_t op1 = (hw1 >> 7) & 3u;
    const bool load = ((hw1 >> 4) & 3u) == 1u;
    const uint32_t op3 = (hw2 >> 4) & 0xFu;
    if (op1 == 1u && op3 != 4u && op3 != 5u) Undefined(bus, Encoding(hw1, hw2));
    const uint32_t size = op1 == 0u ? 4u : op3 == 4u ? 1u : 2u;
    const uint32_t n = hw1 & 0xFu;
    const uint32_t t = hw2 >> 12;
    const uint32_t d = size == 4u ? (hw2 >> 8) & 0xFu : hw2 & 0xFu;
    const uint32_t imm32 = size == 4u ? (hw2 & 0xFFu) * 4u : 0u;
    const uint32_t ones = ((load || size != 4u) ? 0x0F00u : 0u) | ((load && size != 4u) ? 0x000Fu : 0u);
    if ((hw2 & ones) != ones) Unpredictable(bus, Encoding(hw1, hw2));
    if (Is13Or15(t) || n == 15u || (!load && (Is13Or15(d) || d == n || d == t)))
        Unpredictable(bus, Encoding(hw1, hw2));
    const uint32_t address = ReadReg(n) + imm32;
    if ((address & (size - 1u)) != 0u) UnalignedFault(bus);
    if (load) {
        state_.excl_active = 1u;
        state_.excl_tag = address >> 2;
        WriteReg(t, MemRead(bus, address, size));
        PipelinedLoad(1u << n, t);
        return;
    }
    const bool passed = state_.excl_active != 0u && state_.excl_tag == address >> 2;
    ClearExclusiveLocal();
    if (passed) {
        MemWrite(bus, address, size, size == 4u ? ReadReg(t) : ReadReg(t) & ((1u << (size * 8u)) - 1u));
        WriteReg(d, 0u);
        PipelinedLoad(1u << n, d);
        return;
    }
    ++cycles_;
    WriteReg(d, 1u);
    PipelinedLoad(1u << n, d);
}

}

}
