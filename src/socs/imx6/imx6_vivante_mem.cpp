#include "imx6_vivante_mem.h"

#include <cerfcore/core/byte_order.h>

#include <cstring>

namespace cerfcore {

namespace imx6_vivante {

void VivanteMem::StoreStateReg(uint32_t byte_off, uint32_t value) {
    state_registers_.Store(byte_off, value);
}

const uint8_t* VivanteMem::TranslateGpuToHost(uint32_t gpu_addr, size_t size,
                                              MmuClient client) const {
    return mmu_.TranslateToHost(gpu_addr, size, client);
}

uint8_t* VivanteMem::TranslateGpuToHostWrite(uint32_t gpu_addr, size_t size,
                                             MmuClient client) const {
    return mmu_.TranslateToHostWrite(gpu_addr, size, client);
}

bool VivanteMem::DetectIdleRing(uint32_t pc, IdleRingInfo& info) const {
    uint32_t pair[2]{};
    if (!ReadCommandWords(pc, pair, 2u)) return false;

    const uint32_t op = pair[0] >> 27;
    if (op == kFeWait) {
        if ((pair[0] & 0xFFFFu) == 0u) return false;
        uint32_t link[2]{};
        if (!ReadCommandWords(pc + 8u, link, 2u)) return false;
        if ((link[0] >> 27) != kFeLink || (link[0] & kFeCommandPrefetchMask) < 2u) {
            return false;
        }
        if (link[1] != pc) return false;
        info.target = pc;
        info.prefetch = link[0] & kFeCommandPrefetchMask;
        return true;
    }

    if (op != kFeLink || pair[1] == 0u || (pair[0] & kFeCommandPrefetchMask) < 2u) {
        return false;
    }

    uint32_t linked[4]{};
    if (!ReadCommandWords(pair[1], linked, 4u)) {
        return false;
    }
    if ((linked[0] >> 27) != kFeWait || (linked[0] & 0xFFFFu) == 0u || (linked[2] >> 27) != kFeLink ||
        (linked[2] & kFeCommandPrefetchMask) < 2u || linked[3] != pair[1]) {
        return false;
    }
    info.target = pair[1];
    info.prefetch = linked[2] & kFeCommandPrefetchMask;
    return true;
}

const uint8_t* VivanteMem::TranslateCommandToHost(uint32_t address, size_t size) const {
    return TranslateGpuToHost(address, size, MmuClient::Fe);
}

bool VivanteMem::ReadCommandBytes(uint32_t address, void* out_buffer, size_t count) const {
    auto* out = static_cast<uint8_t*>(out_buffer);
    return ReadGpuBytes(address, out, count, MmuClient::Fe);
}

bool VivanteMem::ReadCommandWords(uint32_t address, uint32_t* out, uint32_t count) const {
    uint8_t local_bytes[sizeof(uint32_t) * 4u]{};
    uint8_t* bytes = count <= 4u ? local_bytes : reinterpret_cast<uint8_t*>(out);
    if (!ReadCommandBytes(address, bytes, static_cast<size_t>(count) * sizeof(uint32_t))) return false;
    for (uint32_t i = 0u; i < count; ++i) out[i] = cerf::le::U32(bytes, static_cast<size_t>(i) * 4u);
    return true;
}

bool VivanteMem::FetchCommandWords(uint32_t address, uint32_t* out, uint32_t count) const {
    if (!ReadCommandWords(address, out, count)) return false;
    if (count >= 2u) {
        s_.regs_[0x668u >> 2] = out[count - 2u];
        s_.regs_[0x66Cu >> 2] = out[count - 1u];
    }
    return true;
}

bool VivanteMem::TransferGpuBytes(uint32_t address, const void* input_buffer, void* output_buffer,
                                 size_t count, MmuClient client) const {
    const auto* input = static_cast<const uint8_t*>(input_buffer);
    auto* output = static_cast<uint8_t*>(output_buffer);
    while (count != 0u) {
        const size_t page_left = 0x1000u - (address & 0xFFFu);
        size_t chunk = count;
        if (chunk > page_left) chunk = page_left;
        if (input) {
            uint8_t* destination = TranslateGpuToHostWrite(address, chunk, client);
            if (!destination) return false;
            std::memcpy(destination, input, chunk);
            input += chunk;
        } else {
            const uint8_t* source = TranslateGpuToHost(address, chunk, client);
            if (!source) return false;
            std::memcpy(output, source, chunk);
            output += chunk;
        }
        count -= chunk;
        if (count != 0u) {
            if (chunk > 0xFFFFFFFFu - address) return false;
            address += static_cast<uint32_t>(chunk);
        }
    }
    return true;
}

bool VivanteMem::ReadGpuBytes(uint32_t address, void* out_buffer, size_t count, MmuClient client) const {
    return TransferGpuBytes(address, nullptr, out_buffer, count, client);
}

bool VivanteMem::WriteGpuBytes(uint32_t address, const void* in_buffer, size_t count, MmuClient client) const {
    return TransferGpuBytes(address, in_buffer, nullptr, count, client);
}

}

}
