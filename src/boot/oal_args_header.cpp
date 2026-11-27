#include "oal_args_header.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/emulated_memory.h>

namespace cerfcore {

namespace {

constexpr uint32_t kSignature     = 0x53475241u;
constexpr uint32_t kOalVersionOff = 4u;
constexpr uint32_t kBspVersionOff = 6u;

}

REGISTER_SERVICE(OalArgsHeader);

void OalArgsHeader::Write(uint32_t pa, uint16_t oal_version, uint16_t bsp_version) {
    auto& mem = emu_.Get<EmulatedMemory>();
    mem.WriteWord(pa, kSignature);
    mem.WriteHalf(pa + kOalVersionOff, oal_version);
    mem.WriteHalf(pa + kBspVersionOff, bsp_version);
}

}
