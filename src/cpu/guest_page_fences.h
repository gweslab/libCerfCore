#pragma once
#include <cerfcore/core/force_inline.h>
#include <cerfcore/core/service.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <span>

namespace cerfcore {

class GuestPageFences : public Service {
public:
    using Service::Service;

    struct Range {
        uint32_t pa    = 0;
        uint32_t bytes = 0;
    };

    uint64_t Raise(std::span<const Range> writes, std::span<const Range> reads);
    void     Complete(uint64_t fence);
    void     Fail(std::exception_ptr failure);

    CERF_FORCEINLINE bool Active() const {
        return raised_.load(std::memory_order_acquire) != completed_.load(std::memory_order_acquire);
    }
    CERF_FORCEINLINE void Wait(uint32_t pa, uint64_t bytes, bool write) {
        if (Active()) WaitSlow(pa, bytes, write);
    }
    bool Fenced(uint32_t pa, uint64_t bytes) const;

private:
    static constexpr uint32_t kGranuleShift = 12u;
    static constexpr uint32_t kGranules     = 1u << (32u - kGranuleShift);
    static constexpr uint32_t kRegionShift  = 20u;
    static constexpr uint32_t kRegions      = 1u << (32u - kRegionShift);

    bool Conflicts(uint32_t pa, uint64_t bytes, bool write) const;
    void WaitSlow(uint32_t pa, uint64_t bytes, bool write);
    template <uint32_t kShift, class Visit>
    static void ForEach(uint32_t pa, uint64_t bytes, Visit&& visit);

    std::unique_ptr<std::atomic<uint64_t>[]> write_until_;
    std::unique_ptr<std::atomic<uint64_t>[]> read_until_;
    std::unique_ptr<std::atomic<uint64_t>[]> region_write_until_;
    std::unique_ptr<std::atomic<uint64_t>[]> region_read_until_;
    std::atomic<uint64_t>                    raised_{0};
    std::atomic<uint64_t>                    completed_{0};
    std::mutex                               mutex_;
    std::condition_variable                  completed_signal_;
    std::exception_ptr                       failure_;
};

}
