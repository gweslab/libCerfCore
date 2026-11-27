#pragma once

#include "msm8255_rpc_endpoint.h"

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

namespace cerfcore {

class Msm8255RpcServer;
class StateReader;
class StateWriter;

struct Msm8255ModemCallback {
    uint32_t              prog       = 0u;
    uint32_t              vers       = 0u;
    uint32_t              client_cid = 0u;
    uint32_t              proc       = 0u;
    uint32_t              self_pid   = 0u;
    Msm8255RpcEndpoint    dest;
    std::vector<uint32_t> args;
    std::vector<uint32_t> reply_results;
    uint32_t              key        = 0u;
    int64_t               latency_ns = 0;
    uint32_t              owner      = 0u;
};

class Msm8255ModemCallbacks : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void Queue(Msm8255ModemCallback callback);
    bool Pending(uint32_t prog, uint32_t owner) const;
    void CancelUnsentOf(uint32_t prog, uint32_t owner);
    void ReleaseClient(const Msm8255RpcEndpoint& client);
    uint32_t ConsumeReply(const Msm8255RpcServer& server, const uint8_t* payload, uint32_t size,
                          uint32_t out_pa, uint32_t out_cap);
    uint32_t FlushDue(uint32_t out_pa, uint32_t out_cap);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    struct Entry {
        Msm8255ModemCallback callback;
        uint32_t             xid   = 0u;
        uint32_t             sent  = 0u;
        uint32_t             timed = 0u;
        uint64_t             due   = 0u;
    };

    bool     IsHead(size_t index) const;
    size_t   DueHead(uint64_t now) const;
    void     ScheduleHeads();
    void     Release();
    uint32_t Send(Entry& entry, uint32_t out_pa, uint32_t out_cap);

    std::deque<Entry>       queue_;
    GuestCycleClock::Event* release_  = nullptr;
    uint32_t                next_xid_ = 0u;
};

}
