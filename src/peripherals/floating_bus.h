#pragma once

#include <cstdint>
#include <unordered_set>

namespace cerfcore {

class FloatingBus {
public:
    uint8_t  Read8 (const char* tag, uint32_t addr);
    uint16_t Read16(const char* tag, uint32_t addr);
    uint32_t Read32(const char* tag, uint32_t addr);
    uint64_t Read64(const char* tag, uint32_t addr);
    void     Write (const char* tag, uint32_t bits, uint32_t addr);

private:
    void Trace(const char* tag, bool write, uint32_t bits, uint32_t addr);

    std::unordered_set<uint64_t> traced_;
};

}
