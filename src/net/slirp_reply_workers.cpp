#include "slirp_reply_workers.h"

namespace cerfcore {

SlirpReplyWorkers::~SlirpReplyWorkers() { Stop(); }

void SlirpReplyWorkers::Start(Work work) {
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
    std::thread thread([work = std::move(work), done] {
        work();
        done->store(true, std::memory_order_release);
    });
    workers_.push_back({std::move(thread), std::move(done)});
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
