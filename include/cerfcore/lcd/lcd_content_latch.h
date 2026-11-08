#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace cerfcore {

class EmulatedMemory;

class LcdContentLatch {
public:
    bool ProbeAndLatch(EmulatedMemory& mem, uint32_t fb_pa, size_t fb_bytes);

    bool ProbeAndLatch(const uint8_t* fb, size_t fb_bytes);

    bool Latched() const {
        return latched_.load(std::memory_order_acquire);
    }

    void Rearm();

    bool BeginResetRelease();
    void EndResetRelease();

private:
    std::mutex        mtx_;
    std::atomic<bool> latched_{false};
    bool              awaiting_release_ = false;
    bool              rearmed_          = false;
    bool              have_baseline_    = false;
    uint64_t          baseline_sig_     = 0;
};

}
