#pragma once

#include <cstdint>
#include <optional>

namespace cerfcore {

struct Rcvdl56dpflDownloadBlock {
    uint8_t  mode;
    uint16_t address;
    uint32_t length;
    uint32_t crc32;
};

std::optional<uint8_t> Rcvdl56dpflDownloadCheck(const Rcvdl56dpflDownloadBlock& block);

}
