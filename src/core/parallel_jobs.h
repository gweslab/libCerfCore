#pragma once
#include <cerfcore/core/service.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace cerfcore {

class Fatal;

class ParallelJobs : public Service {
public:
    using Service::Service;
    void OnReady() override;
    void OnShutdown() override;

    void     Require();
    uint32_t Lanes() const;
    void     Run(uint32_t jobs, const std::function<void(uint32_t)>& job);

private:
    struct Batch {
        const std::function<void(uint32_t)>* job = nullptr;
        uint32_t                             jobs = 0;
        std::atomic<uint32_t>                next{0};
        uint32_t                             finished = 0;
        uint32_t                             busy = 0;
        std::vector<std::exception_ptr>      failures;
    };

    void Start();
    void Work();
    void Pull(Batch& batch);
    Batch* OpenBatch();
    std::exception_ptr Execute(const Batch& batch, uint32_t index);

    Fatal* fatal_ = nullptr;
    bool required_ = false;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::vector<Batch*> batches_;
    bool stopping_ = false;
};

}
