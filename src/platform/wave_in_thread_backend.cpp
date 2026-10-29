#include "wave_in_thread_backend.h"

#include <cerfcore/core/instance_threads.h>

namespace cerfcore {

void WaveInThreadBackend::Begin() { thread_ = threads_->Start([this] { ThreadMain(); }); }

void WaveInThreadBackend::Stop() {
    {
        std::lock_guard<std::mutex> lk(mtx_);
        stop_ = true;
        InterruptLocked();
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void WaveInThreadBackend::OnRequest() {
    std::lock_guard<std::mutex> lk(mtx_);
    InterruptLocked();
    cv_.notify_all();
}

}
