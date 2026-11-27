#pragma once

#include "msm8255_rpc_server.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;
struct Msm8255OncrpcRoute;

class Msm8255HandsetRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t ServerProg() const override;
    uint32_t ServerVers() const override;
    uint32_t ServerCid() const override;
    bool     CallbackClientCid(uint32_t& cid) const override;
    void     ReleaseClient(const Msm8255RpcEndpoint& client) override;

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void ReportPowerKey(bool down);

private:
    struct Subscriber {
        uint32_t in_use   = 0u;
        uint32_t handle   = 0u;
        uint32_t callback = 0u;
        uint32_t self_pid = 0u;
        uint32_t peer_pid = 0u;
        uint32_t peer_cid = 0u;
    };

    uint32_t AnswerSubscribe(uint32_t proc, const std::vector<uint32_t>& args,
                             const Msm8255OncrpcRoute& reply);
    uint32_t AnswerCommand(uint32_t proc, const std::vector<uint32_t>& args);
    void     ApplyPowerKeyResetThreshold(uint32_t value);

    Subscriber subscribers_[2]{};
};

}
