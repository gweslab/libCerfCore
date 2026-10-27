#include "wave_in_thread_backend.h"

namespace cerfcore {

void WaveInThreadBackend::Begin() { thread_ = std::thread([this] { ThreadMain(); }); }

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
