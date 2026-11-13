#pragma once

#include <cerfcore/core/service.h>
#include "tick_scale.h"

#include <atomic>
#include <cstdint>

namespace cerfcore {

class VirtualClock : public Service {
public:
    using Service::Service;

    void OnReady() override;

    int64_t NowNs() const;

    void Pause();
    void Resume();

    bool Running() const {
        return running_.load(std::memory_order_acquire);
    }

private:
    int64_t HostTicks() const;

    std::atomic<uint32_t> seq_{0};
    std::atomic<int64_t>  accum_ticks_{0};
    std::atomic<int64_t>  epoch_ticks_{0};
    std::atomic<bool>     running_{true};
    int64_t               host_hz_     = 1;
    int64_t               ns_per_tick_ = 0;
};

}
