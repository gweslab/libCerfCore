#pragma once

#include <cstddef>
#include <cstdint>

namespace cerfcore {

inline constexpr uint8_t kCompaqIpaqH3000MicroSof        = 0x02;
inline constexpr uint8_t kCompaqIpaqH3000MicroMaxPayload = 15;
inline constexpr size_t  kCompaqIpaqH3000MicroMaxFrame   = 3u + kCompaqIpaqH3000MicroMaxPayload;

size_t BuildCompaqIpaqH3000MicroFrame(uint8_t msg_id, const uint8_t* data, uint8_t len,
                                      uint8_t* out);

}
