#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/core/callback_list.h>

#include <functional>

namespace cerfcore {

class Sh77xxStandbyEntry : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RegisterListener(std::function<void()> listener);

private:
    void Enter();

    CallbackList<> listeners_;
};

}
