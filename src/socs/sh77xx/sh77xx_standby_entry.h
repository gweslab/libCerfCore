#pragma once

#include <cerfcore/core/service.h>

#include <functional>
#include <vector>

namespace cerfcore {

class Sh77xxStandbyEntry : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void RegisterListener(std::function<void()> listener);

private:
    void Enter();

    std::vector<std::function<void()>> listeners_;
};

}
