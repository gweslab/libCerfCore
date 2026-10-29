#pragma once

#include "network_backend.h"
#include "slirp_reply_workers.h"
#include "../platform/host_dns_proxy.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct Slirp;
struct SlirpCb;

namespace cerfcore {

class Fatal;
class InstanceThreads;
struct SlirpTimer;

class SlirpBackend : public NetworkBackend {
public:
    using NetworkBackend::NetworkBackend;

    bool ShouldRegister() override;
    void OnReady() override;    /* construct libslirp + start thread */
    void OnShutdown() override;

    ~SlirpBackend() override;

    void SendFrame(const uint8_t* frame, std::size_t len) override;

    bool TryInterceptIcmpEcho(const uint8_t* frame, std::size_t len);

    bool TryInterceptAaaaQuery(const uint8_t* frame, std::size_t len);
    cerf::inet::MacAddress GuestMacAddress() const override;

    /* Implementation hooks for the C callbacks libslirp invokes. Public so
       the static C-shim functions in the .cpp can call them; do not call
       these from outside the .cpp. */
    void OnSlirpSendPacket(const void* buf, std::size_t len);
    int64_t OnSlirpClockGetNs();
    SlirpTimer* OnSlirpTimerNew(void (*cb)(void* opaque), void* cb_opaque);
    void OnSlirpTimerFree(SlirpTimer* t);
    void OnSlirpTimerMod(SlirpTimer* t, int64_t expire_ms);
    void OnSlirpNotify();

private:
    void PollLoop();
    void StopPollThread();
    void RethrowCallbackFailure();
    int64_t NowMs() const;

    Fatal*             fatal_   = nullptr;
    InstanceThreads*   threads_ = nullptr;
    std::exception_ptr callback_failure_;

    cerf::inet::MacAddress guest_mac_{};
    uint32_t mtu_ = 1500;
    bool host_has_v6_ = false;
    uint32_t vhost_n_ = 0;
    uint32_t vnameserver_n_ = 0;

    Slirp* slirp_ = nullptr;
    SlirpCb* cbs_ = nullptr;
    std::mutex slirp_mutex_;          /* serializes ALL libslirp calls */

    std::thread poll_thread_;

    SlirpReplyWorkers                            icmp_replies_;
    std::unique_ptr<host_platform::HostDnsProxy> dns_proxy_;

    std::atomic<bool> stop_{false};
    std::mutex notify_mutex_;
    std::condition_variable notify_cv_;
    bool notified_ = false;

    std::mutex timers_mutex_;
    std::vector<std::unique_ptr<SlirpTimer>> timers_;
};

}
