#pragma once

#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_server.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255NpaRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t ServerProg() const override;
    uint32_t ServerVers() const override;
    uint32_t ServerCid() const override;
    void ReleaseClient(const Msm8255RpcEndpoint& client) override;
    bool CallbackClientCid(uint32_t& cid) const override;

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void ReadDefineNodeArgs(const Msm8255OncrpcCall& call, uint32_t size,
                            uint32_t& callback, uint32_t& object);
    void ReadDefineResourceArgs(const Msm8255OncrpcCall& call, uint32_t size,
                                uint32_t& callback, uint32_t& object);
    void ReadCreateClientArgs(const Msm8255OncrpcCall& call, uint32_t size,
                              uint32_t& type, uint32_t& supplied,
                              std::string& resource);
    uint32_t AnswerCreateClient(const Msm8255OncrpcCall& call, uint32_t size,
                                const Msm8255OncrpcRoute& reply);
    uint32_t AnswerIssueRequest(const Msm8255OncrpcCall& call, uint32_t size,
                                const Msm8255OncrpcRoute& reply);
    void QueueCallback(const Msm8255OncrpcRoute& reply, uint32_t cb_index, uint32_t node);
};

}
