#include "floating_bus.h"

#include <cerfcore/core/log.h>

namespace cerfcore {

void FloatingBus::Trace(const char* tag, bool write, uint32_t bits, uint32_t addr) {
    const uint64_t key = (static_cast<uint64_t>(addr) << 8) | (static_cast<uint64_t>(write) << 7) | bits;
    if (!traced_.insert(key).second) return;
    LOG(Periph, "[%s] %c%u 0x%08X (floating bus, first access)\n", tag, write ? 'w' : 'r', bits, addr);
}

uint8_t FloatingBus::Read8(const char* tag, uint32_t addr) {
    Trace(tag, false, 8u, addr);
    return 0xFFu;
}

uint16_t FloatingBus::Read16(const char* tag, uint32_t addr) {
    Trace(tag, false, 16u, addr);
    return 0xFFFFu;
}

uint32_t FloatingBus::Read32(const char* tag, uint32_t addr) {
    Trace(tag, false, 32u, addr);
    return 0xFFFFFFFFu;
}

uint64_t FloatingBus::Read64(const char* tag, uint32_t addr) {
    Trace(tag, false, 64u, addr);
    return 0xFFFFFFFFFFFFFFFFull;
}

void FloatingBus::Write(const char* tag, uint32_t bits, uint32_t addr) {
    Trace(tag, true, bits, addr);
}

}
