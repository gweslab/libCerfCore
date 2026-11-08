#include "display_size_latch.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_presentation.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

bool DisplaySizeLatch::PublishOnce(CerfEmulator& emu, bool display_enabled) {
    if (!display_enabled || published_.exchange(true, std::memory_order_acq_rel)) return false;
    emu.Get<HostPresentation>().PanelEnabled();
    return true;
}

void DisplaySizeLatch::Reset() {
    published_.store(false, std::memory_order_release);
}

void DisplaySizeLatch::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("published", published_.load(std::memory_order_acquire) ? 1u : 0u);
}

void DisplaySizeLatch::RestoreState(StateReader& r) {
    uint8_t v = 0;
    r.Read("published", v);
    published_.store(v != 0, std::memory_order_release);
}

}
