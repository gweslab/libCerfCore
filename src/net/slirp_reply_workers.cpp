#include "slirp_reply_workers.h"

#include <cerfcore/core/instance_threads.h>

namespace cerfcore {

SlirpReplyWorkers::~SlirpReplyWorkers() { Stop(); }

void SlirpReplyWorkers::Start(InstanceThreads& threads, Work work) {
    auto done = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lk(mutex_);
    if (stopping_) return;
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (it->done->load(std::memory_order_acquire)) {
            it->thread.join();
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
    workers_.push_back(
        {threads.Start(std::move(work), [done] { done->store(true, std::memory_order_release); }), done});
}

void SlirpReplyWorkers::Stop() {
    std::vector<Worker> live;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stopping_ = true;
        live.swap(workers_);
    }
    for (Worker& w : live)
        if (w.thread.joinable()) w.thread.join();
}

}
