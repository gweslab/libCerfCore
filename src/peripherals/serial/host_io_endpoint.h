#pragma once

#include "serial_endpoint.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <utility>

namespace cerfcore {

class CerfEmulator;

class HostIoEndpoint : public SerialEndpoint {
public:
    using DeadCallback = std::function<void(const char* reason, uint32_t code)>;

    explicit HostIoEndpoint(CerfEmulator& emu) : emu_(emu) {}

    void SetOnBridgeDead(DeadCallback cb) { on_dead_ = std::move(cb); }

protected:
    void ReportDead(const char* reason, uint32_t code);
    bool WithGuestAccess(const std::function<void()>& touch);

    virtual bool WaitStopped(uint32_t timeout_ms) = 0;

    CerfEmulator&     emu_;
    std::atomic<bool> running_{false};

private:
    std::atomic<bool> dead_{false};
    DeadCallback      on_dead_;
};

}
