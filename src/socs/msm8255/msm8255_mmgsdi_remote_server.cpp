#include "msm8255_modem_callbacks.h"
#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_client_table.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kMmgsdiProg        = 0x30000019u;
constexpr uint32_t kMmgsdiVers        = 0x000A0001u;
constexpr uint32_t kMmgsdiCid         = 17u;
constexpr uint32_t kMmgsdiCbProg      = 0x31000019u;
constexpr uint32_t kMmgsdiCbVers      = 0x000A0001u;
constexpr uint32_t kMmgsdiCbClientCid = 18u;

constexpr uint32_t kProcClientIdReg    = 19u;
constexpr uint32_t kProcClientEvtReg   = 20u;
constexpr uint32_t kProcClientIdDereg  = 22u;
constexpr uint32_t kProcClientEvtDereg = 23u;
constexpr uint32_t kCbProcResponse   = 1u;
constexpr uint32_t kCbProcEvent      = 2u;

constexpr uint32_t kStatusOk         = 0u;
constexpr uint32_t kCnfClientIdReg   = 0u;
constexpr uint32_t kCnfClientEvtReg  = 1u;
constexpr uint32_t kCnfClientIdDereg  = 2u;
constexpr uint32_t kCnfClientEvtDereg = 3u;
constexpr uint32_t kEvtCardError     = 1u;
constexpr uint32_t kGuestSlot        = 1u;
constexpr uint32_t kCardErrorStateTwo = 2u;

class Msm8255MmgsdiRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { clients_.Clear(); });
    }

    uint32_t ServerProg() const override { return kMmgsdiProg; }
    uint32_t ServerVers() const override { return kMmgsdiVers; }
    uint32_t ServerCid() const override { return kMmgsdiCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    bool CallbackClientCid(uint32_t& cid) const override {
        cid = kMmgsdiCbClientCid;
        return true;
    }

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void QueueResponse(uint32_t callback, uint32_t cnf, uint32_t client, uint32_t data,
                       uint32_t self_pid, const Msm8255RpcEndpoint& dest);
    uint32_t RequireIssuedClient(const char* request, uint32_t high, uint32_t low);

    Msm8255RpcClientTable<Msm8255RpcClientId> clients_{UINT32_MAX};
};

uint32_t Msm8255MmgsdiRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                                               uint32_t out_cap, uint32_t self_pid,
                                               uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
    const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kMmgsdiCid, peer_pid, peer_cid,
                                   call.xid};
    const Msm8255RpcEndpoint source{peer_pid, peer_cid};

    switch (call.proc) {
    case kProcClientIdReg: {
        codec.RequireArgWords(*this, call.proc, w, 2u);
        const Msm8255RpcClientId* client = clients_.Issue();
        if (!client) {
            emu_.Get<Fatal>().Die(
                "msm8255 mmgsdi remote server: client id registration after %u issued ids",
                clients_.LastId());
        }
        const uint32_t id = client->id;
        LOG(SocModem, "msm8255 mmgsdi remote server: client id %u, data 0x%08X\n", id, w[1]);
        if (w[0] != kRpcNoCallback) {
            QueueResponse(w[0], kCnfClientIdReg, id, w[1], self_pid, source);
        }
        break;
    }
    case kProcClientEvtReg: {
        codec.RequireArgWords(*this, call.proc, w, 5u);
        const uint32_t id = RequireIssuedClient("event registration", w[0], w[1]);
        if (w[2] == kRpcNoCallback) {
            emu_.Get<Fatal>().Die(
                "msm8255 mmgsdi remote server: event registration of client %u with no event "
                "callback is not modeled", id);
        }
        LOG(SocModem, "msm8255 mmgsdi remote server: client %u events, card error %u on slot %u\n",
            id, kCardErrorStateTwo, kGuestSlot);
        if (w[3] != kRpcNoCallback) {
            QueueResponse(w[3], kCnfClientEvtReg, id, w[4], self_pid, source);
        }
        emu_.Get<Msm8255ModemCallbacks>().Queue(Msm8255ModemCallback{
            kMmgsdiCbProg, kMmgsdiCbVers, kMmgsdiCbClientCid, kCbProcEvent, self_pid, source,
            {w[2], kXdrTrue, kEvtCardError, 0u, id, 0u, 0u, kEvtCardError, kGuestSlot,
             kCardErrorStateTwo},
            {}, 0u, 0});
        break;
    }
    case kProcClientIdDereg: {
        codec.RequireArgWords(*this, call.proc, w, 4u);
        const uint32_t id = RequireIssuedClient("client id deregistration", w[0], w[1]);
        clients_.Remove(id);
        LOG(SocModem, "msm8255 mmgsdi remote server: client %u deregistered\n", id);
        if (w[2] != kRpcNoCallback) {
            QueueResponse(w[2], kCnfClientIdDereg, id, w[3], self_pid, source);
        }
        break;
    }
    case kProcClientEvtDereg: {
        codec.RequireArgWords(*this, call.proc, w, 5u);
        const uint32_t id = RequireIssuedClient("event deregistration", w[0], w[1]);
        codec.RequireNoCallback(*this, call.proc, w[2]);
        LOG(SocModem, "msm8255 mmgsdi remote server: client %u events deregistered\n", id);
        if (w[3] != kRpcNoCallback) {
            QueueResponse(w[3], kCnfClientEvtDereg, id, w[4], self_pid, source);
        }
        break;
    }
    default:
        emu_.Get<Fatal>().Die("msm8255 mmgsdi remote server: procedure %u is not modeled",
                              call.proc);
    }
    const uint32_t results[1] = {kStatusOk};
    return codec.WriteAcceptedReply(reply, results, 1u);
}

void Msm8255MmgsdiRemoteServer::QueueResponse(uint32_t callback, uint32_t cnf, uint32_t client,
                                              uint32_t data, uint32_t self_pid,
                                              const Msm8255RpcEndpoint& dest) {
    emu_.Get<Msm8255ModemCallbacks>().Queue(Msm8255ModemCallback{
        kMmgsdiCbProg, kMmgsdiCbVers, kMmgsdiCbClientCid, kCbProcResponse, self_pid, dest,
        {callback, kStatusOk, cnf, kXdrTrue, cnf, 0u, client, 0u, 0u, 0u, 0u, 0u, kStatusOk, data,
         0u, 0u, 0u, 0u},
        {}, 0u, 0});
}

uint32_t Msm8255MmgsdiRemoteServer::RequireIssuedClient(const char* request, uint32_t high,
                                                        uint32_t low) {
    if (high != 0u || !clients_.Find(low)) {
        emu_.Get<Fatal>().Die(
            "msm8255 mmgsdi remote server: %s names client 0x%08X%08X, which this server never "
            "issued", request, high, low);
    }
    return low;
}

void Msm8255MmgsdiRemoteServer::SaveState(StateWriter& w) { clients_.Save(w, "mmgsdi"); }

void Msm8255MmgsdiRemoteServer::RestoreState(StateReader& r) { clients_.Restore(r, "mmgsdi"); }

}

REGISTER_SERVICE(Msm8255MmgsdiRemoteServer);

}
