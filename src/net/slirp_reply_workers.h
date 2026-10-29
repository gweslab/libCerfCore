#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cerfcore {

class InstanceThreads;

class SlirpReplyWorkers {
public:
    using Work = std::function<void()>;

    SlirpReplyWorkers() = default;
    ~SlirpReplyWorkers();
    SlirpReplyWorkers(const SlirpReplyWorkers&)            = delete;
    SlirpReplyWorkers& operator=(const SlirpReplyWorkers&) = delete;

    void Start(InstanceThreads& threads, Work work);
    void Stop();

private:
    struct Worker {
        std::thread                        thread;
        std::shared_ptr<std::atomic<bool>> done;
    };

    std::mutex          mutex_;
    std::vector<Worker> workers_;
    bool                stopping_ = false;
};

}
