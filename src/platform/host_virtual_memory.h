#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace host_platform {

uint8_t* AllocateZeroed(size_t size);
void     Release(uint8_t* memory);

uint8_t* AllocateZeroedTracked(size_t size);
bool     TouchedPages(uint8_t* memory, size_t size, size_t page_bytes, std::vector<uint8_t>& bitmap);
bool     ZeroPages(uint8_t* memory, size_t size, size_t page_bytes, const std::vector<uint8_t>& bitmap);

}

}
