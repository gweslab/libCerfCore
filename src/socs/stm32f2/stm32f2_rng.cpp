#include "stm32f2_rng.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

uint32_t Rng::Read(uint32_t offset, uint32_t size) {
    if (size != 4u) mcu_.Fault("STM32F2 RNG: sub-word access", kBase + offset);
    switch (offset) {
    case 0x00u: return cr_;
    case 0x04u:
        if ((cr_ & 4u) == 0u) return 0u;
        CheckClock();
        return 1u;
    case 0x08u:
        if ((cr_ & 4u) == 0u) mcu_.Fault("STM32F2 RNG: DR read with RNGEN clear", cr_);
        CheckClock();
        state_ ^= state_ >> 12;
        state_ ^= state_ << 25;
        state_ ^= state_ >> 27;
        return static_cast<uint32_t>((state_ * 0x2545F4914F6CDD1DULL) >> 32);
    default: mcu_.Fault("STM32F2 RNG: read of an unmodelled register", kBase + offset);
    }
    CERF_UNREACHABLE();
}

void Rng::CheckClock() {
    if (rcc_.Pll48Hz() * 16u < rcc_.HclkHz()) mcu_.Fault("STM32F2 RNG: an RNG_CLK clock error is not modelled", cr_);
}

void Rng::Write(uint32_t offset, uint32_t size, uint32_t value) {
    if (size != 4u) mcu_.Fault("STM32F2 RNG: sub-word access", kBase + offset);
    if (offset == 0x00u) {
        if ((value & ~0xCu) != 0u) mcu_.Fault("STM32F2 RNG: reserved RNG_CR bits", value);
        if ((value & 8u) != 0u) mcu_.Fault("STM32F2 RNG: interrupt is not modelled", value);
        cr_ = value & 0xCu;
        return;
    }
    if (offset == 0x04u) {
        if ((value & ~0x67u) != 0u) mcu_.Fault("STM32F2 RNG: RNG_SR write to reserved bits", value);
        return;
    }
    mcu_.Fault("STM32F2 RNG: write of an unmodelled register", kBase + offset);
}

void Rng::SaveState(StateWriter& writer) const {
    writer.Write("rng_cr", cr_);
    writer.Write("rng_state", state_);
}

void Rng::RestoreState(StateReader& reader) {
    reader.Read("rng_cr", cr_);
    reader.Read("rng_state", state_);
}

}

}
