#pragma once

#include <cstdint>

namespace cerfcore {

struct Imx6MmioLane {
    uint32_t address;
    uint32_t value;
    uint32_t mask;

    uint32_t Merge(uint32_t current) const {
        return (current & ~mask) | (value & mask);
    }
};

enum class Imx6MmioWidth { Byte, Half };

template <typename ReadWord>
uint8_t Imx6ReadMmioByte(uint32_t address, ReadWord read_word) {
    const uint32_t shift = (address & 3u) * 8u;
    return static_cast<uint8_t>(read_word(address & ~3u) >> shift);
}

template <typename ReadWord>
uint16_t Imx6ReadMmioHalf(uint32_t address, ReadWord read_word) {
    const uint32_t shift = (address & 3u) * 8u;
    const uint32_t low = read_word(address & ~3u) >> shift;
    if (shift <= 16u) return static_cast<uint16_t>(low);
    return static_cast<uint16_t>(low | (read_word((address & ~3u) + 4u) << 8u));
}

inline Imx6MmioLane Imx6DecodeMmioLane(uint32_t address, uint32_t value, Imx6MmioWidth width) {
    const uint32_t shift = (address & 3u) * 8u;
    const uint32_t lane_mask = width == Imx6MmioWidth::Byte ? 0xFFu : 0xFFFFu;
    return {address & ~3u, (value & lane_mask) << shift, lane_mask << shift};
}

template <typename Callback>
void Imx6ForEachMmioLane(uint32_t address, uint32_t value, Imx6MmioWidth width, Callback callback) {
    if (width == Imx6MmioWidth::Half && (address & 3u) == 3u) {
        callback(Imx6DecodeMmioLane(address, value, Imx6MmioWidth::Byte));
        callback(Imx6DecodeMmioLane(address + 1u, value >> 8u, Imx6MmioWidth::Byte));
        return;
    }
    callback(Imx6DecodeMmioLane(address, value, width));
}

}
