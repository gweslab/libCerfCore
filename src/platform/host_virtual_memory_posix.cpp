#include "host_virtual_memory.h"

#include "host_page_state.h"

#include <cerrno>
#include <sys/mman.h>
#include <unistd.h>

#include <cerfcore/core/log.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace host_platform {

namespace {

size_t PageBytes() { return static_cast<size_t>(::sysconf(_SC_PAGESIZE)); }

bool Marked(const std::vector<uint8_t>& bitmap, size_t page) { return (bitmap[page >> 3u] >> (page & 7u)) & 1u; }

uint8_t* AlignDown(uint8_t* p, uintptr_t host) {
    return reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(p) & ~(host - 1u));
}

uint8_t* AlignUp(uint8_t* p, uintptr_t host) {
    return reinterpret_cast<uint8_t*>((reinterpret_cast<uintptr_t>(p) + host - 1u) & ~(host - 1u));
}

bool ZeroRun(uint8_t* first, uint8_t* last, const uint8_t* region_end) {
    const uintptr_t host = PageBytes();
    uint8_t* const  lo   = AlignUp(first, host);
    uint8_t* const  hi   = last == region_end ? AlignUp(last, host) : AlignDown(last, host);
    if (lo >= hi) {
        std::memset(first, 0, static_cast<size_t>(last - first));
        return true;
    }
    std::memset(first, 0, static_cast<size_t>(lo - first));
    if (hi < last) std::memset(hi, 0, static_cast<size_t>(last - hi));
    return DiscardWholePages(lo, static_cast<size_t>(hi - lo));
}

}

uint8_t* AllocateZeroed(size_t size) {
    const size_t header = PageBytes();
    void* base = ::mmap(nullptr, header + size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (base == MAP_FAILED) {
        LOG(Caution, "mmap of 0x%zX bytes failed (errno=%d)\n", size, errno);
        return nullptr;
    }
    *static_cast<size_t*>(base) = header + size;
    return static_cast<uint8_t*>(base) + header;
}

uint8_t* AllocateZeroedTracked(size_t size) { return AllocateZeroed(size); }

void Release(uint8_t* memory) {
    uint8_t* const base = memory - PageBytes();
    ::munmap(base, *reinterpret_cast<const size_t*>(base));
}

bool TouchedPages(uint8_t* memory, size_t size, size_t page_bytes, std::vector<uint8_t>& bitmap) {
    std::fill(bitmap.begin(), bitmap.end(), uint8_t{0});
    return MarkTouchedPages(memory, size, page_bytes, bitmap);
}

bool ZeroPages(uint8_t* memory, size_t size, size_t page_bytes, const std::vector<uint8_t>& bitmap) {
    uint8_t* const region_end = memory + size;
    size_t         page       = 0;
    while (page * page_bytes < size) {
        if (!Marked(bitmap, page)) {
            ++page;
            continue;
        }
        size_t end = page;
        while (end * page_bytes < size && Marked(bitmap, end)) ++end;
        if (!ZeroRun(memory + page * page_bytes, std::min(memory + end * page_bytes, region_end), region_end))
            return false;
        page = end;
    }
    return true;
}

}

}
