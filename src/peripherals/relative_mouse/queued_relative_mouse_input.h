#pragma once

#include <cerfcore/host/relative_mouse_input.h>

#include <cstdint>
#include <mutex>
#include <vector>

namespace cerfengine { class HostRequestChannel; }

namespace cerfcore {

class QueuedRelativeMouseInput : public RelativeMouseInput {
public:
    using RelativeMouseInput::RelativeMouseInput;

    void OnReady() final;
    void OnRelativeMove(int dx, int dy, uint32_t button_mask) final;

protected:
    virtual void OnMouseReady() = 0;
    virtual void ApplyHostMotion(int dx, int dy, uint32_t button_mask) = 0;

private:
    struct HostMotion {
        int      dx      = 0;
        int      dy      = 0;
        uint32_t buttons = 0;
    };

    void OnHostRequest();

    cerfengine::HostRequestChannel* requests_ = nullptr;
    std::mutex                      host_mtx_;
    std::vector<HostMotion>         host_motion_;
};

}
