#include "adreno_a2xx_pixel_queue.h"
#include "adreno_a2xx_pixel_fences.h"
#include "adreno_a2xx_pixel_stage.h"
#include "adreno_a2xx_socs.h"
#include "../core/parallel_jobs.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/core/log.h>
#include <cerfcore/execution/jit_runner.h>
#include <cerfcore/state/emulation_freeze.h>
#include <cerfcore/state/hibernation.h>
#include <algorithm>
#include <functional>

namespace cerfcore {

REGISTER_SERVICE(AdrenoA2xxPixelQueue);

bool AdrenoA2xxPixelQueue::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxPixelQueue::OnReady() {
    fences_  = &emu_.Get<GuestPageFences>();
    collect_ = &emu_.Get<AdrenoA2xxPixelFences>();
    stage_   = &emu_.Get<AdrenoA2xxPixelStage>();
    freeze_  = &emu_.Get<EmulationFreeze>();
    fatal_   = &emu_.Get<Fatal>();
    workers_ = &emu_.Get<ParallelJobs>();
    emu_.Get<JitRunner>().RegisterPreStartHook([this] { Start(); });
    emu_.Get<Hibernation>().RegisterPreSaveListener([this] { SettleUnderSnapshot(); });
}

void AdrenoA2xxPixelQueue::Start() {
    thread_ = emu_.Get<InstanceThreads>().Start([this] { Work(); });
}

void AdrenoA2xxPixelQueue::OnShutdown() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    work_.notify_all();
    idle_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void AdrenoA2xxPixelQueue::Submit(AdrenoA2xxPrimitiveJob job) {
    writes_.clear();
    reads_.clear();
    collect_->Collect(job, writes_, reads_);
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [&] { return failure_ || stopping_ || queue_.size() < kMaxQueued; });
    if (failure_) {
        const std::exception_ptr failure = failure_;
        lock.unlock();
        Fatal::Raise(failure);
    }
    if (stopping_) {
        lock.unlock();
        Settle();
        stage_->Execute(job);
        return;
    }
    const uint64_t fence = fences_->Raise(writes_, reads_);
    queue_.push_back({std::move(job), fence});
    lock.unlock();
    work_.notify_one();
}

void AdrenoA2xxPixelQueue::Close(const Entry& entry, const std::exception_ptr& failure) {
    if (failure) fences_->Fail(failure);
    else         fences_->Complete(entry.fence);
}

std::exception_ptr AdrenoA2xxPixelQueue::Run(Entry& entry) {
    std::exception_ptr failure;
    {
        const Fatal::CatchScope plain;
        failure = fatal_->RunForWaiter([&] { stage_->Execute(entry.job); });
    }
    Close(entry, failure);
    return failure;
}

void AdrenoA2xxPixelQueue::Work() {
    for (;;) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_.wait(lock, [&] { return stopping_ || (!queue_.empty() && !settling_); });
            if (stopping_ && (queue_.empty() || settling_)) return;
        }
        {
            const auto section = freeze_->WorkerSection();
            {
                const std::lock_guard<std::mutex> lock(mutex_);
                if (queue_.empty() || settling_) continue;
                current_ = std::move(queue_.front());
                queue_.pop_front();
                running_ = true;
                taken_   = false;
            }
            stage_->Begin(current_.job);
        }
        std::exception_ptr failure;
        {
            const Fatal::CatchScope plain;
            failure = fatal_->RunForWaiter([&] { DriveCurrent(); });
        }
        if (failure) {
            const auto section = freeze_->WorkerSection();
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!taken_) fences_->Fail(failure);
            failure_ = failure;
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            taken_   = false;
        }
        idle_.notify_all();
        if (failure) Fatal::Raise(failure);
    }
}

void AdrenoA2xxPixelQueue::DriveCurrent() {
    for (;;) {
        uint32_t generation = 0, lanes = 0;
        {
            const auto section = freeze_->WorkerSection();
            if (taken_) return;
            if (!stage_->Step()) {
                fences_->Complete(current_.fence);
                const std::lock_guard<std::mutex> lock(mutex_);
                running_ = false;
                return;
            }
            generation = stage_->BandGeneration();
            lanes = (std::min)(stage_->Bands(), workers_->Lanes());
        }
        const std::function<void(uint32_t)> lane = [this, generation](uint32_t) {
            for (;;) {
                const auto section = freeze_->WorkerSection();
                if (!stage_->RunNextBand(generation)) return;
            }
        };
        if (lanes < 2u) lane(0u);
        else            workers_->Run(lanes, lane);
    }
}

void AdrenoA2xxPixelQueue::Settle() {
    std::unique_lock<std::mutex> lock(mutex_);
    settling_ = true;
    idle_.wait(lock, [&] { return !running_; });
    Drain(lock, true);
}

void AdrenoA2xxPixelQueue::SettleUnderSnapshot() {
    std::unique_lock<std::mutex> lock(mutex_);
    settling_ = true;
    if (running_ && !taken_ && !failure_) {
        taken_ = true;
        lock.unlock();
        LOG(SocGpu, "a2xx pixel queue: state save completes the pixel job of fence %llu in flight\n",
            static_cast<unsigned long long>(current_.fence));
        std::exception_ptr failure;
        {
            const Fatal::CatchScope plain;
            failure = fatal_->RunForWaiter([&] { stage_->Complete(); });
        }
        Close(current_, failure);
        lock.lock();
        if (failure) failure_ = failure;
    }
    Drain(lock, false);
}

void AdrenoA2xxPixelQueue::Drain(std::unique_lock<std::mutex>& lock, bool raise) {
    std::exception_ptr failure = failure_;
    while (!failure && !queue_.empty()) {
        Entry entry = std::move(queue_.front());
        queue_.pop_front();
        lock.unlock();
        failure = Run(entry);
        lock.lock();
        if (failure) failure_ = failure;
    }
    settling_ = false;
    lock.unlock();
    work_.notify_all();
    idle_.notify_all();
    if (failure && raise) Fatal::Raise(failure);
}

void AdrenoA2xxPixelQueue::ResetStage() {
    Settle();
    stage_->PowerOnReset();
}

}
