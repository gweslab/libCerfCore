#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/core/callback_list.h>

#include <functional>

namespace cerfcore {

class GuestCpuReset;

class Pr31x00ResetLine : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RegisterResetListener(std::function<void()> fn);
    void RegisterReleaseListener(std::function<void()> fn);

    bool Resetting() const;

private:
    GuestCpuReset*                     reset_ = nullptr;
    CallbackList<>                     reset_listeners_;
    CallbackList<>                     release_listeners_;
};

}
