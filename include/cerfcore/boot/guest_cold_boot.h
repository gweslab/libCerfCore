#pragma once

#include "../core/service.h"
#include "../state/state_stream.h"
#include "../core/callback_list.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>

namespace cerfcore {

class GuestColdBoot : public Service {
public:
    using Service::Service;

    /* Replays run in registration order. Reordering lets a later
       re-placement overwrite an earlier writer's patches (the
       guest-additions TOC patches land on top of RomPlacer's copy). */
    void RegisterReplay(std::function<void()> fn);

    /* Snapshot len bytes at pa now; replay writes them back verbatim.
       Replayed RAM must be byte-identical across resets - flash keeps
       KVAs pointing at it, and an allocating producer (GuestModulePlacer
       lowers the DLL-RW floor on every run) shifts them when re-run. */
    void RecordPatch(uint32_t pa, uint32_t len);
    void ClearRecordedPatches();

    void RequestHardReset();
    void RequestHardResetFromHost();

    void ExecuteIfPending();

    bool Pending() const {
        return pending_.load(std::memory_order_acquire) || fill_pending_.load(std::memory_order_acquire);
    }
    void ReplayBootWrites();

    void RequestResetFillingRam(std::function<void()> fill);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    CallbackList<>                     replays_;
    uint32_t                           patch_generation_ = 0;
    size_t                             live_patches_     = 0;
    size_t                             cleared_patches_  = 0;
    std::atomic<bool>                  pending_{false};
    std::function<void()>              fill_;
    std::atomic<bool>                  fill_pending_{false};
};

}
