#pragma once

#include <cstdint>
#include <vector>

namespace cerfcore {

constexpr uint32_t kMsm8255QmiTlvHeaderBytes = 3u;

struct Msm8255QmiTlv {
    uint8_t  type   = 0u;
    uint16_t length = 0u;
    uint32_t value  = 0u;
};

void AppendMsm8255QmiTlv(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value);
bool ReadMsm8255QmiTlv(const uint8_t* bytes, uint32_t size, uint32_t offset, Msm8255QmiTlv& tlv);

}
