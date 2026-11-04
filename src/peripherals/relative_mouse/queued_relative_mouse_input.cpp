#include "queued_relative_mouse_input.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/hibernation.h>
#include <cerfengine/host_request_channel.h>
#include "../../execution/engine_bridge.h"

namespace cerfcore {

void QueuedRelativeMouseInput::OnReady() {
    requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
    requests_->RegisterListener([this] { OnHostRequest(); });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
        std::lock_guard<std::mutex> lk(host_mtx_);
        host_motion_.clear();
    });
    OnMouseReady();
}

void QueuedRelativeMouseInput::OnRelativeMove(int dx, int dy, uint32_t button_mask) {
    {
        std::lock_guard<std::mutex> lk(host_mtx_);
        if (host_motion_.empty() || host_motion_.back().buttons != button_mask) {
            host_motion_.push_back({0, 0, button_mask});
        }
        host_motion_.back().dx += dx;
        host_motion_.back().dy += dy;
    }
    requests_->Request();
}

void QueuedRelativeMouseInput::OnHostRequest() {
    std::vector<HostMotion> motion;
    {
        std::lock_guard<std::mutex> lk(host_mtx_);
        motion.swap(host_motion_);
    }
    for (const HostMotion& m : motion) ApplyHostMotion(m.dx, m.dy, m.buttons);
}

}
