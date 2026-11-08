#include <cerfcore/lcd/lcd_content_latch.h>

#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>

namespace cerfcore {

namespace {

constexpr size_t kProbeStride = 251;

}

bool LcdContentLatch::ProbeAndLatch(EmulatedMemory& mem,
                                    uint32_t fb_pa,
                                    size_t   fb_bytes) {
    if (latched_.load(std::memory_order_acquire)) return true;
    if (fb_bytes == 0)                            return false;

    const uint8_t* fb = mem.TryTranslate(fb_pa);
    if (!fb) return false;
    return ProbeAndLatch(fb, fb_bytes);
}

bool LcdContentLatch::ProbeAndLatch(const uint8_t* fb, size_t fb_bytes) {
    if (latched_.load(std::memory_order_acquire)) return true;
    if (!fb || fb_bytes == 0)                     return false;

    std::lock_guard<std::mutex> lk(mtx_);
    if (latched_.load(std::memory_order_relaxed)) return true;
    if (awaiting_release_)                        return false;

    bool     nonzero = false;
    uint64_t sig     = 14695981039346656037ull;
    for (size_t i = 0; i < fb_bytes; i += kProbeStride) {
        const uint8_t b = fb[i];
        nonzero |= (b != 0);
        sig = (sig ^ b) * 1099511628211ull;
    }

    if (rearmed_) {
        if (!have_baseline_) {
            baseline_sig_  = sig;
            have_baseline_ = true;
            return false;
        }
        if (!nonzero) {
            baseline_sig_ = sig;
            return false;
        }
        if (sig == baseline_sig_) return false;
        rearmed_       = false;
        have_baseline_ = false;
    }

    if (!nonzero) return false;
    latched_.store(true, std::memory_order_release);
    LOG(Lcd, "content latch: fresh content latched (sig=0x%016llX)\n",
        (unsigned long long)sig);
    return true;
}

void LcdContentLatch::Rearm() {
    std::lock_guard<std::mutex> lk(mtx_);
    awaiting_release_ = true;
    latched_.store(false, std::memory_order_release);
    LOG(Lcd, "content latch: rearmed (guest reset)\n");
}

bool LcdContentLatch::BeginResetRelease() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (!awaiting_release_) return false;
    have_baseline_    = false;
    rearmed_          = true;
    awaiting_release_ = false;
    return true;
}

void LcdContentLatch::EndResetRelease() {
    std::lock_guard<std::mutex> lk(mtx_);
    if (have_baseline_) {
        LOG(Lcd, "content latch: reset released over a shown frame (sig=0x%016llX)\n",
            (unsigned long long)baseline_sig_);
        return;
    }
    rearmed_ = false;
    LOG(Lcd, "content latch: reset released with no frame shown\n");
}

}
