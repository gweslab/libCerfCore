#pragma once

#include "wave_in_backend.h"

#include <condition_variable>
#include <mutex>
#include <thread>

namespace cerfcore {

class WaveInThreadBackend : public WaveInBackend {
public:
    void Stop() final;

protected:
    void Begin() final;
    void OnRequest() final;

    virtual void ThreadMain() = 0;
    virtual void InterruptLocked() {}

    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    stop_ = false;

private:
    std::thread thread_;
};

}
