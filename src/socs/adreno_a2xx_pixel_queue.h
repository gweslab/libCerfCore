#pragma once
#include "adreno_a2xx_pixel_job.h"
#include "../cpu/guest_page_fences.h"
#include <cerfcore/core/service.h>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace cerfcore {

class AdrenoA2xxPixelFences;
class AdrenoA2xxPixelStage;
class EmulationFreeze;
class Fatal;
class ParallelJobs;

class AdrenoA2xxPixelQueue : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;
    void Submit(AdrenoA2xxPrimitiveJob job);
    void ResetStage();

private:
    struct Entry {
        AdrenoA2xxPrimitiveJob job;
        uint64_t               fence = 0;
    };
    static constexpr size_t kMaxQueued = 1024u;

    void Start();
    void Work();
    void DriveCurrent();
    void Settle();
    void SettleUnderSnapshot();
    void Drain(std::unique_lock<std::mutex>& lock, bool raise);
    void Close(const Entry& entry, const std::exception_ptr& failure);
    std::exception_ptr Run(Entry& entry);

    GuestPageFences*                    fences_  = nullptr;
    AdrenoA2xxPixelFences*              collect_ = nullptr;
    AdrenoA2xxPixelStage*               stage_   = nullptr;
    EmulationFreeze*                    freeze_  = nullptr;
    Fatal*                              fatal_   = nullptr;
    ParallelJobs*                       workers_ = nullptr;
    std::thread                         thread_;
    std::mutex                          mutex_;
    std::condition_variable             work_;
    std::condition_variable             idle_;
    std::deque<Entry>                   queue_;
    Entry                               current_;
    bool                                running_  = false;
    bool                                taken_    = false;
    bool                                settling_ = false;
    bool                                stopping_ = false;
    std::exception_ptr                  failure_;
    std::vector<GuestPageFences::Range> writes_;
    std::vector<GuestPageFences::Range> reads_;
};

}
