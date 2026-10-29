#include "silent_block_pacer.h"

#include "wave_pcm_format.h"

#include <cerfcore/core/fatal.h>

#include <algorithm>

namespace cerfcore {

bool SilentBlockPacer::Queue(Fatal& fatal, uintptr_t block, uint32_t length, uint32_t rate_hz,
                             uint16_t channels, uint16_t bits, const char* log_tag) {
    for (uint32_t i = 0; i < count_; ++i) {
        if (blocks_[i].id != block) continue;
        fatal.Die("%s: block %llu is already awaiting a silent completion", log_tag,
                  static_cast<unsigned long long>(block));
    }
    if (count_ == kCapacity) {
        fatal.Die("%s: %u silent blocks already pending", log_tag, kCapacity);
    }
    const uint64_t   bytes_per_sec = static_cast<uint64_t>(rate_hz) * PcmFrameBytes(channels, bits);
    Clock::duration period        = Clock::duration::zero();
    if (bytes_per_sec != 0u) {
        period = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(
            static_cast<double>(length) / static_cast<double>(bytes_per_sec)));
    }
    if (period == Clock::duration::zero()) {
        fatal.Die("%s: cannot pace a silent block at %u Hz x %u ch x %u bit", log_tag,
                  rate_hz, channels, bits);
    }
    tail_             = std::max(Clock::now(), tail_) + period;
    blocks_[count_++] = Block{block, tail_};
    return count_ == 1;
}

uint32_t SilentBlockPacer::TakeDue(Clock::time_point now, uintptr_t (&done)[kCapacity]) {
    uint32_t n = 0;
    while (n < count_ && blocks_[n].due <= now) {
        done[n] = blocks_[n].id;
        ++n;
    }
    count_ -= n;
    for (uint32_t i = 0; i < count_; ++i) blocks_[i] = blocks_[i + n];
    return n;
}

uint32_t SilentBlockPacer::TakeAll(uintptr_t (&done)[kCapacity]) {
    const uint32_t n = count_;
    for (uint32_t i = 0; i < n; ++i) done[i] = blocks_[i].id;
    count_ = 0;
    tail_  = Clock::time_point{};
    return n;
}

}
