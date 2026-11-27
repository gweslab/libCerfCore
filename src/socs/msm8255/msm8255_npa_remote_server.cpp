#include "msm8255_npa_remote_server.h"

#include "msm8255_modem_callbacks.h"
#include "msm8255_npa_clients.h"
#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kNpaProg   = 0x300000A4u;
constexpr uint32_t kNpaVers   = 0x00010001u;
constexpr uint32_t kNpaCid    = 1u;
constexpr uint32_t kNpaResult = 0u;

constexpr uint32_t kProcDefineNode     = 2u;
constexpr uint32_t kProcCreateClient   = 3u;
constexpr uint32_t kProcIssueRequest   = 6u;
constexpr uint32_t kProcDefineResource = 22u;

constexpr uint32_t kClientTypeMax = 9u;

constexpr uint32_t kDefineResultWords = 1u;

constexpr uint32_t kIssueRequestPayloadBytes = kPacmarkBytes + kCallArgsOff + 12u;

constexpr uint32_t kReqArgHandleOff   = kCallArgsOff +  0u;
constexpr uint32_t kReqArgRequestOff  = kCallArgsOff +  4u;
constexpr uint32_t kReqArgSuppliedOff = kCallArgsOff +  8u;

constexpr uint32_t kCallPayloadBytes = 64u;

constexpr uint32_t kCallArg0Off    = kCallArgsOff +  0u;
constexpr uint32_t kCallArg1Off    = kCallArgsOff +  4u;
constexpr uint32_t kCallArg2Off    = kCallArgsOff +  8u;
constexpr uint32_t kCallArgCbOff   = kCallArgsOff + 12u;
constexpr uint32_t kCallArgNodeOff = kCallArgsOff + 16u;

constexpr uint32_t kDefineArg0       = 1u;

constexpr uint32_t kCbProg      = 0x310000A4u;
constexpr uint32_t kCbProc      = 1u;
constexpr uint32_t kCbClientCid = 2u;

}

bool Msm8255NpaRemoteServer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255NpaRemoteServer::OnReady() {
    emu_.Get<Msm8255RpcServerRegistry>().Register(this);
}

uint32_t Msm8255NpaRemoteServer::ServerProg() const { return kNpaProg; }
uint32_t Msm8255NpaRemoteServer::ServerVers() const { return kNpaVers; }
uint32_t Msm8255NpaRemoteServer::ServerCid() const { return kNpaCid; }
void Msm8255NpaRemoteServer::ReleaseClient(const Msm8255RpcEndpoint&) {}
bool Msm8255NpaRemoteServer::CallbackClientCid(uint32_t& cid) const {
    cid = kCbClientCid;
    return true;
}

uint32_t Msm8255NpaRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                            uint32_t out_pa, uint32_t out_cap,
                                            uint32_t self_pid,
                                            uint32_t peer_pid,
                                            uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);

    if (call.proc != kProcDefineNode && call.proc != kProcDefineResource &&
        call.proc != kProcCreateClient && call.proc != kProcIssueRequest) {
        emu_.Get<Fatal>().Die(
            "msm8255 npa remote server: rpc procedure %u with a %u-byte payload "
            "is not modeled", call.proc, size);
    }

    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kNpaCid, peer_pid, peer_cid, call.xid};
    if (call.proc == kProcCreateClient) {
        return AnswerCreateClient(call, size, reply);
    }
    if (call.proc == kProcIssueRequest) {
        return AnswerIssueRequest(call, size, reply);
    }

    uint32_t callback = 0u;
    uint32_t node     = 0u;
    if (call.proc == kProcDefineNode) {
        ReadDefineNodeArgs(call, size, callback, node);
    } else {
        ReadDefineResourceArgs(call, size, callback, node);
    }

    const uint32_t results[kDefineResultWords] = {kNpaResult};
    if (callback != kRpcNoCallback) {
        QueueCallback(reply, callback, node);
    }
    return codec.WriteAcceptedReply(reply, results, kDefineResultWords);
}

void Msm8255NpaRemoteServer::ReadDefineNodeArgs(const Msm8255OncrpcCall& call,
                                                uint32_t size, uint32_t& callback,
                                                uint32_t& object) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireCallBytes(*this, kProcDefineNode, size, kCallPayloadBytes);

    const uint32_t arg0 = codec.CallWord(call, kCallArg0Off);
    const uint32_t arg1 = codec.CallWord(call, kCallArg1Off);
    const uint32_t arg2 = codec.CallWord(call, kCallArg2Off);
    if (arg0 != kDefineArg0 || arg1 != 0u || arg2 != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 npa remote server: rpc call arguments %u %u %u are not the "
            "request this peer models", arg0, arg1, arg2);
    }

    callback = codec.CallWord(call, kCallArgCbOff);
    object   = codec.CallWord(call, kCallArgNodeOff);
}

void Msm8255NpaRemoteServer::ReadDefineResourceArgs(const Msm8255OncrpcCall& call,
                                                    uint32_t size,
                                                    uint32_t& callback,
                                                    uint32_t& object) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const uint32_t off  = codec.ConsumeXdrString(call.body, size, kCallArgsOff, 1u);
    const uint32_t want = kPacmarkBytes + off + 8u;
    codec.RequireCallBytes(*this, kProcDefineResource, size, want);

    callback = codec.CallWord(call, off);
    object   = codec.CallWord(call, off + 4u);
}

void Msm8255NpaRemoteServer::ReadCreateClientArgs(const Msm8255OncrpcCall& call,
                                                  uint32_t size, uint32_t& type,
                                                  uint32_t& supplied,
                                                  std::string& resource) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    const uint32_t named  = codec.ConsumeXdrString(call.body, size, kCallArgsOff, 1u, &resource);
    const uint32_t client = codec.ConsumeXdrString(call.body, size, named, 2u);
    const uint32_t want   = kPacmarkBytes + client + 8u;
    codec.RequireCallBytes(*this, kProcCreateClient, size, want);

    type     = codec.CallWord(call, client);
    supplied = codec.CallWord(call, client + 4u);
    if (type > kClientTypeMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 npa remote server: the create-client call asks for client "
            "type %u, and the guest encodes only types 0 through %u", type,
            kClientTypeMax);
    }
    codec.RequireBoolean(*this, kProcCreateClient, supplied);
}

uint32_t Msm8255NpaRemoteServer::AnswerCreateClient(const Msm8255OncrpcCall& call,
                                                    uint32_t size,
                                                    const Msm8255OncrpcRoute& reply) {
    uint32_t    type     = 0u;
    uint32_t    supplied = 0u;
    std::string resource;
    ReadCreateClientArgs(call, size, type, supplied, resource);

    const uint32_t handle = emu_.Get<Msm8255NpaClients>().IssueHandle(resource);
    return emu_.Get<Msm8255OncrpcCodec>().WriteOptionalReply(reply, kNpaResult, supplied, handle);
}

uint32_t Msm8255NpaRemoteServer::AnswerIssueRequest(const Msm8255OncrpcCall& call,
                                                    uint32_t size,
                                                    const Msm8255OncrpcRoute& reply) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireCallBytes(*this, kProcIssueRequest, size, kIssueRequestPayloadBytes);

    const uint32_t handle   = codec.CallWord(call, kReqArgHandleOff);
    const uint32_t request  = codec.CallWord(call, kReqArgRequestOff);
    const uint32_t supplied = codec.CallWord(call, kReqArgSuppliedOff);

    codec.RequireBoolean(*this, kProcIssueRequest, supplied);
    const uint32_t state = emu_.Get<Msm8255NpaClients>().ApplyRequest(handle, request);
    return codec.WriteOptionalReply(reply, kNpaResult, supplied, state);
}

void Msm8255NpaRemoteServer::QueueCallback(const Msm8255OncrpcRoute& reply, uint32_t cb_index,
                                           uint32_t node) {
    emu_.Get<Msm8255ModemCallbacks>().Queue(Msm8255ModemCallback{
        kCbProg, kNpaVers, kCbClientCid, kCbProc, reply.self_pid,
        Msm8255RpcEndpoint{reply.peer_pid, reply.peer_cid}, {cb_index, node, 0u, 0u, 0u},
        {kNpaResult}, 0u, 0});
}

void Msm8255NpaRemoteServer::SaveState(StateWriter& w) {
    emu_.Get<Msm8255NpaClients>().SaveState(w);
}

void Msm8255NpaRemoteServer::RestoreState(StateReader& r) {
    emu_.Get<Msm8255NpaClients>().RestoreState(r);
}

REGISTER_SERVICE(Msm8255NpaRemoteServer);

}
