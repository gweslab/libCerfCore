#pragma once

#include "../core/service.h"
#include "../state/state_stream.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <vector>

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

    /* Any thread. Arms the cold boot and triggers reset delivery. */
    void RequestHardReset();

    void ExecuteIfPending();

    void ReplayBootWrites();

    void RequestResetFillingRam(std::function<void()> fill);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    std::vector<std::function<void()>> replays_;
    std::atomic<bool>                  pending_{false};
    std::function<void()>              fill_;
    std::atomic<bool>                  fill_pending_{false};
};

}
