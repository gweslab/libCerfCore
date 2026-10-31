#include "host_virtual_memory.h"

#define NOMINMAX
#include <windows.h>

#include <cerfcore/core/log.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace host_platform {

namespace {

uint8_t* Allocate(size_t size, DWORD extra_type) {
    void* memory = ::VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE | extra_type, PAGE_READWRITE);
    if (!memory) {
        LOG(Caution, "VirtualAlloc of 0x%zX bytes failed (GLE=%lu)\n", size, ::GetLastError());
    }
    return static_cast<uint8_t*>(memory);
}

}

uint8_t* AllocateZeroed(size_t size) { return Allocate(size, 0); }

uint8_t* AllocateZeroedTracked(size_t size) { return Allocate(size, MEM_WRITE_WATCH); }

void Release(uint8_t* memory) {
    ::VirtualFree(memory, 0, MEM_RELEASE);
}

bool TouchedPages(uint8_t* memory, size_t size, size_t page_bytes, std::vector<uint8_t>& bitmap) {
    std::fill(bitmap.begin(), bitmap.end(), uint8_t{0});
    std::vector<void*> touched(size / page_bytes + 1u);
    ULONG_PTR          count       = touched.size();
    DWORD              granularity = 0;
    if (::GetWriteWatch(0, memory, size, touched.data(), &count, &granularity) != 0) return false;
    for (ULONG_PTR i = 0; i < count; ++i) {
        const size_t start = static_cast<size_t>(static_cast<uint8_t*>(touched[i]) - memory);
        const size_t end   = std::min(size, start + static_cast<size_t>(granularity));
        for (size_t page = start / page_bytes; page * page_bytes < end; ++page)
            bitmap[page >> 3u] |= static_cast<uint8_t>(1u << (page & 7u));
    }
    return true;
}

bool ZeroPages(uint8_t* memory, size_t size, size_t page_bytes, const std::vector<uint8_t>& bitmap) {
    for (size_t page = 0; page * page_bytes < size; ++page) {
        if (!((bitmap[page >> 3u] >> (page & 7u)) & 1u)) continue;
        uint8_t* const at = memory + page * page_bytes;
        std::memset(at, 0, page_bytes);
        ::ResetWriteWatch(at, page_bytes);
    }
    return true;
}

}

}
