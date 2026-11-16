#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace cerf::zlib_inflate {

std::vector<uint8_t> Decompress(const uint8_t* src, size_t src_size, size_t out_size, size_t* consumed = nullptr);

}

}
