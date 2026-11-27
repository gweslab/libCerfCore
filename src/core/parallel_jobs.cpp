#include "parallel_jobs.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/execution/jit_runner.h>
#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(ParallelJobs);

void ParallelJobs::OnReady() {
    fatal_ = &emu_.Get<Fatal>();
    emu_.Get<JitRunner>().RegisterPreStartHook([this] { Start(); });
}

void ParallelJobs::Require() { required_ = true; }

void ParallelJobs::Start() {
    if (!required_) return;
    const unsigned lanes = std::thread::hardware_concurrency() / 2u;
    auto& threads = emu_.Get<InstanceThreads>();
    for (unsigned i = 1; i < lanes; ++i) threads_.push_back(threads.Start([this] { Work(); }));
}

void ParallelJobs::OnShutdown() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& thread : threads_) thread.join();
}

uint32_t ParallelJobs::Lanes() const {
    return static_cast<uint32_t>(threads_.size()) + 1u;
}

std::exception_ptr ParallelJobs::Execute(const Batch& batch, uint32_t index) {
    const Fatal::CatchScope plain;
    return fatal_->RunForWaiter([&batch, index] { (*batch.job)(index); });
}

void ParallelJobs::Pull(Batch& batch) {
    for (uint32_t index = batch.next.fetch_add(1u); index < batch.jobs; index = batch.next.fetch_add(1u)) {
        std::exception_ptr failure = Execute(batch, index);
        const std::lock_guard<std::mutex> lock(mutex_);
        batch.failures[index] = std::move(failure);
        if (++batch.finished == batch.jobs) idle_.notify_all();
    }
}

ParallelJobs::Batch* ParallelJobs::OpenBatch() {
    for (Batch* batch : batches_)
        if (batch->next.load(std::memory_order_relaxed) < batch->jobs) return batch;
    return nullptr;
}

void ParallelJobs::Work() {
    for (;;) {
        Batch* batch = nullptr;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [&] { return stopping_ || (batch = OpenBatch()) != nullptr; });
            if (stopping_) return;
            ++batch->busy;
        }
        Pull(*batch);
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            --batch->busy;
        }
        idle_.notify_all();
    }
}

void ParallelJobs::Run(uint32_t jobs, const std::function<void(uint32_t)>& job) {
    Batch batch;
    batch.job  = &job;
    batch.jobs = jobs;
    bool parallel = jobs >= 2u && !threads_.empty();
    if (parallel) {
        const std::lock_guard<std::mutex> lock(mutex_);
        parallel = !stopping_;
        if (parallel) {
            batch.failures.assign(jobs, nullptr);
            batches_.push_back(&batch);
        }
    }
    if (!parallel) {
        for (uint32_t index = 0; index < jobs; ++index) job(index);
        return;
    }
    wake_.notify_all();
    Pull(batch);
    std::exception_ptr failure;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        idle_.wait(lock, [&] { return batch.finished == batch.jobs && batch.busy == 0u; });
        batches_.erase(std::find(batches_.begin(), batches_.end(), &batch));
        for (const auto& candidate : batch.failures) if (candidate) { failure = candidate; break; }
    }
    if (failure) Fatal::Raise(failure);
}

}
