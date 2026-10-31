#include "host_page_state.h"

#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>

namespace cerfcore {

namespace host_platform {

bool MarkTouchedPages(uint8_t* memory, size_t size, size_t page_bytes, std::vector<uint8_t>& bitmap) {
    const size_t     host = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
    std::vector<int> dispositions((size + host - 1u) / host);
    mach_vm_size_t   count = dispositions.size();
    if (::mach_vm_page_range_query(::mach_task_self(), reinterpret_cast<mach_vm_offset_t>(memory), size,
                                   reinterpret_cast<mach_vm_address_t>(dispositions.data()), &count) != KERN_SUCCESS)
        return false;
    for (size_t i = 0; i < count; ++i) {
        if ((dispositions[i] & (VM_PAGE_QUERY_PAGE_PRESENT | VM_PAGE_QUERY_PAGE_PAGED_OUT)) == 0) continue;
        const size_t end = std::min(size, (i + 1u) * host);
        for (size_t page = i * host / page_bytes; page * page_bytes < end; ++page)
            bitmap[page >> 3u] |= static_cast<uint8_t>(1u << (page & 7u));
    }
    return true;
}

bool DiscardWholePages(uint8_t* first, size_t bytes) {
    return ::mmap(first, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) != MAP_FAILED;
}

}

}
