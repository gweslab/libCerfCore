#include "host_page_state.h"

#include <sys/mman.h>

#include <cstring>

namespace cerfcore {

namespace host_platform {

namespace {

bool PageHoldsData(const uint8_t* page, size_t page_bytes) {
    uint64_t any = 0;
    for (size_t i = 0; i < page_bytes; i += sizeof(uint64_t)) {
        uint64_t word = 0;
        std::memcpy(&word, page + i, sizeof(word));
        any |= word;
    }
    return any != 0u;
}

}

bool MarkTouchedPages(uint8_t* memory, size_t size, size_t page_bytes, std::vector<uint8_t>& bitmap) {
    for (size_t page = 0; page * page_bytes < size; ++page)
        if (PageHoldsData(memory + page * page_bytes, page_bytes))
            bitmap[page >> 3u] |= static_cast<uint8_t>(1u << (page & 7u));
    return true;
}

bool DiscardWholePages(uint8_t* first, size_t bytes) {
    return ::madvise(first, bytes, MADV_DONTNEED) == 0;
}

}

}
