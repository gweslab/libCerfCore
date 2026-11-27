#include "guest_page_fences.h"
#include "../execution/engine_bridge.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfengine/guest_engine.h>
#include <algorithm>
#include <vector>

namespace cerfcore {

REGISTER_SERVICE(GuestPageFences);

template <uint32_t kShift, class Visit>
void GuestPageFences::ForEach(uint32_t pa, uint64_t bytes, Visit&& visit) {
    if (bytes == 0u) return;
    const uint64_t first = pa >> kShift;
    const uint64_t last  = (uint64_t(pa) + bytes - 1u) >> kShift;
    for (uint64_t unit = first; unit <= last && unit < (uint64_t{1} << (32u - kShift)); ++unit)
        visit(static_cast<uint32_t>(unit));
}

uint64_t GuestPageFences::Raise(std::span<const Range> writes, std::span<const Range> reads) {
    if (!write_until_) {
        write_until_        = std::make_unique<std::atomic<uint64_t>[]>(kGranules);
        read_until_         = std::make_unique<std::atomic<uint64_t>[]>(kGranules);
        region_write_until_ = std::make_unique<std::atomic<uint64_t>[]>(kRegions);
        region_read_until_  = std::make_unique<std::atomic<uint64_t>[]>(kRegions);
    }
    const uint64_t fence     = raised_.load(std::memory_order_relaxed) + 1u;
    const uint64_t completed = completed_.load(std::memory_order_acquire);
    std::vector<uint32_t> fresh;
    auto mark = [&](std::atomic<uint64_t>* until, std::atomic<uint64_t>* region_until, const Range& range) {
        ForEach<kGranuleShift>(range.pa, range.bytes, [&](uint32_t granule) {
            if (write_until_[granule].load(std::memory_order_relaxed) <= completed &&
                read_until_[granule].load(std::memory_order_relaxed) <= completed)
                fresh.push_back(granule);
            until[granule].store(fence, std::memory_order_relaxed);
        });
        ForEach<kRegionShift>(range.pa, range.bytes, [&](uint32_t region) {
            region_until[region].store(fence, std::memory_order_relaxed);
        });
    };
    for (const Range& range : writes) mark(write_until_.get(), region_write_until_.get(), range);
    for (const Range& range : reads) mark(read_until_.get(), region_read_until_.get(), range);
    raised_.store(fence, std::memory_order_release);
    if (fresh.empty()) return fence;
    std::sort(fresh.begin(), fresh.end());
    auto& engine = emu_.Get<EngineBridge>().Guest();
    for (size_t first = 0; first < fresh.size();) {
        size_t last = first;
        while (last + 1u < fresh.size() && fresh[last + 1u] <= fresh[last] + 1u) ++last;
        engine.DropPhysicalRange(fresh[first] << kGranuleShift, (fresh[last] - fresh[first] + 1u) << kGranuleShift);
        first = last + 1u;
    }
    return fence;
}

void GuestPageFences::Complete(uint64_t fence) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        completed_.store(fence, std::memory_order_release);
    }
    completed_signal_.notify_all();
}

void GuestPageFences::Fail(std::exception_ptr failure) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        failure_ = std::move(failure);
    }
    completed_signal_.notify_all();
}

bool GuestPageFences::Fenced(uint32_t pa, uint64_t bytes) const {
    return Active() && Conflicts(pa, bytes, true);
}

bool GuestPageFences::Conflicts(uint32_t pa, uint64_t bytes, bool write) const {
    const uint64_t completed = completed_.load(std::memory_order_acquire);
    bool conflict = false;
    ForEach<kRegionShift>(pa, bytes, [&](uint32_t region) {
        if (conflict || (region_write_until_[region].load(std::memory_order_relaxed) <= completed &&
                         (!write || region_read_until_[region].load(std::memory_order_relaxed) <= completed)))
            return;
        const uint64_t region_begin = uint64_t(region) << kRegionShift;
        const uint64_t begin = (std::max)(uint64_t(pa), region_begin);
        const uint64_t end   = (std::min)(uint64_t(pa) + bytes, region_begin + (uint64_t{1} << kRegionShift));
        ForEach<kGranuleShift>(static_cast<uint32_t>(begin), end - begin, [&](uint32_t granule) {
            conflict = conflict || write_until_[granule].load(std::memory_order_relaxed) > completed ||
                       (write && read_until_[granule].load(std::memory_order_relaxed) > completed);
        });
    });
    return conflict;
}

void GuestPageFences::WaitSlow(uint32_t pa, uint64_t bytes, bool write) {
    if (!Conflicts(pa, bytes, write)) return;
    std::exception_ptr failure;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        completed_signal_.wait(lock, [&] { return failure_ || !Conflicts(pa, bytes, write); });
        failure = failure_;
    }
    if (failure) Fatal::Raise(failure);
}

}
