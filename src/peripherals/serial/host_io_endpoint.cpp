#include "host_io_endpoint.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/emulation_freeze.h>

namespace cerfcore {

void HostIoEndpoint::ReportDead(const char* reason, uint32_t code) {
    if (dead_.exchange(true)) return;
    on_dead_(reason, code);
}

bool HostIoEndpoint::WithGuestAccess(const std::function<void()>& touch) {
    auto& freeze = emu_.Get<EmulationFreeze>();
    while (running_.load()) {
        auto frozen = freeze.TryWorkerSection();
        if (frozen.owns_lock()) {
            touch();
            return true;
        }
        if (WaitStopped(1)) return false;
    }
    return false;
}

}
