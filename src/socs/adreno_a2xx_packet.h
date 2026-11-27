#pragma once

#include <cstdint>

namespace cerfcore {

enum class AdrenoA2xxPacketSource { Ring, IndirectBuffer };

struct AdrenoA2xxPacket {
    uint64_t address = 0;
    uint32_t header = 0;
    uint32_t type = 0;
    uint32_t opcode = 0;
    uint32_t payload_count = 0;
    bool same_register = false;
    bool predicate = false;
    AdrenoA2xxPacketSource source = AdrenoA2xxPacketSource::Ring;

    static const char* Decode(uint32_t header, uint64_t address, uint32_t available_dwords,
                              AdrenoA2xxPacketSource source, AdrenoA2xxPacket& packet);
    bool OperandAddress(uint32_t index, uint64_t& address) const;
};

}
