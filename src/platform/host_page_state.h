#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace host_platform {

bool MarkTouchedPages(uint8_t* memory, size_t size, size_t page_bytes, std::vector<uint8_t>& bitmap);
bool DiscardWholePages(uint8_t* first, size_t bytes);

}

}
