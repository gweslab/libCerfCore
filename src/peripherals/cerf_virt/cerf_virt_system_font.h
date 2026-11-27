#pragma once

#include <cerfcore/core/service.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

class CerfVirtSystemFont : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool    Set() const { return set_.load(std::memory_order_acquire); }
    int32_t Height() const { return height_.load(std::memory_order_acquire); }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    void LatchConfigured();

    std::atomic<bool>    set_{false};
    std::atomic<int32_t> height_{0};
};

}
