#include "msm8255_modem_callbacks.h"
#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_client_table.h"
#include "msm8255_rpc_endpoint.h"
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

constexpr uint32_t kWmsProg = 0x30000003u;
constexpr uint32_t kWmsVers = 0x00030001u;
constexpr uint32_t kWmsCid  = 16u;
constexpr uint32_t kWmsCbProg        = 0x31000003u;
constexpr uint32_t kWmsCbVers        = 0x00030002u;
constexpr uint32_t kWmsCbClientCid   = 22u;

constexpr uint32_t kProcClientRelease        = 3u;
constexpr uint32_t kProcClientCall4          = 4u;
constexpr uint32_t kProcRegisterBcMmCallback = 11u;
constexpr uint32_t kProcSetRoutes            = 15u;
constexpr uint32_t kProcFlagsCommand21       = 21u;
constexpr uint32_t kProcSetMemoryFull        = 22u;
constexpr uint32_t kProcRegisterMsgCallback  = 80u;
constexpr uint32_t kProcRegisterCfgCallback  = 84u;
constexpr uint32_t kProcClientInit           = 85u;

constexpr uint32_t kCbProcCmd      = 1u;
constexpr uint32_t kCbProcCfgEvent = 2u;

constexpr uint32_t kCfgEventMemoryStatusSet = 9u;
constexpr uint32_t kNotifyCustomFilter      = 3u;
constexpr uint32_t kSetMemoryFullCmd        = 0u;
constexpr uint32_t kCmdErrNone              = 0u;

constexpr uint32_t kWmsOk      = 0u;
constexpr uint32_t kClientMax  = 0xFFu;
constexpr uint32_t kRouteWords = 25u;

struct WmsClient {
    uint32_t           id       = 0u;
    uint32_t           cfg_cb   = kRpcNoCallback;
    uint32_t           cfg_mask = 0u;
    uint32_t           self_pid = 0u;
    Msm8255RpcEndpoint dest;

    template <typename F>
    static constexpr void Visit(WmsClient& c, F& field) {
        field("wms_client_id", c.id);
        field("wms_client_cfg_cb", c.cfg_cb);
        field("wms_client_cfg_mask", c.cfg_mask);
        field("wms_client_self_pid", c.self_pid);
        field("wms_client_dest_pid", c.dest.pid);
        field("wms_client_dest_cid", c.dest.cid);
    }
};

class Msm8255WmsRemoteServer : public Msm8255RpcServer {
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

    uint32_t ServerProg() const override { return kWmsProg; }
    uint32_t ServerVers() const override { return kWmsVers; }
    uint32_t ServerCid() const override { return kWmsCid; }

    void ReleaseClient(const Msm8255RpcEndpoint& client) override {
        for (WmsClient& c : clients_.Clients()) {
            if (c.dest == client) c.cfg_cb = kRpcNoCallback;
        }
    }

    bool CallbackClientCid(uint32_t& cid) const override {
        cid = kWmsCbClientCid;
        return true;
    }

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    uint32_t AnswerClientInit(const Msm8255OncrpcCall& call, const std::vector<uint32_t>& w,
                              const Msm8255OncrpcRoute& reply);
    void QueueSetMemoryFull(const std::vector<uint32_t>& w, uint32_t self_pid,
                            const Msm8255RpcEndpoint& source);
    WmsClient& RequireClient(uint32_t proc, uint32_t id);

    Msm8255RpcClientTable<WmsClient> clients_{kClientMax};
};

uint32_t Msm8255WmsRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                                            uint32_t out_cap, uint32_t self_pid,
                                            uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
    const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kWmsCid, peer_pid, peer_cid,
                                   call.xid};
    const Msm8255RpcEndpoint source{peer_pid, peer_cid};

    switch (call.proc) {
    case kProcClientInit:
        return AnswerClientInit(call, w, reply);
    case kProcRegisterMsgCallback:
        codec.RequireArgWords(*this, call.proc, w, 4u);
        RequireClient(call.proc, w[0]);
        break;
    case kProcRegisterCfgCallback: {
        codec.RequireArgWords(*this, call.proc, w, 4u);
        WmsClient& c = RequireClient(call.proc, w[0]);
        if (w[1] != kNotifyCustomFilter) {
            emu_.Get<Fatal>().Die(
                "msm8255 wms remote server: client %u config registration with notify type %u",
                w[0], w[1]);
        }
        c.cfg_mask = w[2];
        c.cfg_cb   = w[3];
        c.self_pid = self_pid;
        c.dest     = source;
        break;
    }
    case kProcSetMemoryFull:
        codec.RequireArgWords(*this, call.proc, w, 4u);
        RequireClient(call.proc, w[0]);
        codec.RequireBoolean(*this, call.proc, w[3]);
        QueueSetMemoryFull(w, self_pid, source);
        break;
    case kProcRegisterBcMmCallback:
        codec.RequireArgWords(*this, call.proc, w, 2u);
        RequireClient(call.proc, w[0]);
        break;
    case kProcClientCall4:
        codec.RequireArgWords(*this, call.proc, w, 1u);
        RequireClient(call.proc, w[0]);
        break;
    case kProcClientRelease: {
        codec.RequireArgWords(*this, call.proc, w, 1u);
        RequireClient(call.proc, w[0]);
        emu_.Get<Msm8255ModemCallbacks>().CancelUnsentOf(kWmsCbProg, w[0]);
        clients_.Remove(w[0]);
        LOG(SocModem, "msm8255 wms remote server: client %u released\n", w[0]);
        break;
    }
    case kProcFlagsCommand21:
        codec.RequireArgWords(*this, call.proc, w, 5u);
        RequireClient(call.proc, w[0]);
        codec.RequireNoCallback(*this, call.proc, w[1]);
        codec.RequireBoolean(*this, call.proc, w[3]);
        codec.RequireBoolean(*this, call.proc, w[4]);
        break;
    case kProcSetRoutes:
        codec.RequireArgWordsAtLeast(*this, call.proc, w, 4u);
        RequireClient(call.proc, w[0]);
        codec.RequireNoCallback(*this, call.proc, w[1]);
        if (w[3] != kXdrTrue || w.size() != 4u + kRouteWords) {
            emu_.Get<Fatal>().Die(
                "msm8255 wms remote server: routes with pointer word %u in %u argument words "
                "are not modeled", w[3], static_cast<uint32_t>(w.size()));
        }
        codec.RequireBoolean(*this, call.proc, w.back());
        break;
    default:
        emu_.Get<Fatal>().Die("msm8255 wms remote server: procedure %u is not modeled", call.proc);
    }
    const uint32_t results[1] = {kWmsOk};
    return codec.WriteAcceptedReply(reply, results, 1u);
}

uint32_t Msm8255WmsRemoteServer::AnswerClientInit(const Msm8255OncrpcCall& call,
                                                  const std::vector<uint32_t>& w,
                                                  const Msm8255OncrpcRoute& reply) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWordsAtLeast(*this, call.proc, w, 3u);
    const uint32_t present = w[1];
    codec.RequireBoolean(*this, call.proc, present);
    if (w.size() != (present == kXdrTrue ? 4u : 3u)) {
        emu_.Get<Fatal>().Die(
            "msm8255 wms remote server: client init with pointer word %u in %u argument words",
            present, static_cast<uint32_t>(w.size()));
    }
    const WmsClient* client = clients_.Issue();
    if (!client) {
        emu_.Get<Fatal>().Die(
            "msm8255 wms remote server: client init of type %u after %u issued client ids",
            w[0], clients_.LastId());
    }
    LOG(SocModem, "msm8255 wms remote server: client init of type %u, last word %u, client %u\n",
        w[0], w.back(), client->id);
    return codec.WriteOptionalReply(reply, kWmsOk, present, client->id);
}

void Msm8255WmsRemoteServer::QueueSetMemoryFull(const std::vector<uint32_t>& w,
                                                uint32_t self_pid,
                                                const Msm8255RpcEndpoint& source) {
    auto& callbacks = emu_.Get<Msm8255ModemCallbacks>();
    if (w[1] != kRpcNoCallback) {
        callbacks.Queue(Msm8255ModemCallback{kWmsCbProg, kWmsCbVers, kWmsCbClientCid, kCbProcCmd,
                                             self_pid, source,
                                             {w[1], kSetMemoryFullCmd, w[2], kCmdErrNone}, {}, 0u,
                                             0});
    }
    LOG(SocModem, "msm8255 wms remote server: client %u sets memory full to %u\n", w[0], w[3]);
    for (const WmsClient& c : clients_.Clients()) {
        if (c.cfg_cb == kRpcNoCallback || (c.cfg_mask & (1u << kCfgEventMemoryStatusSet)) == 0u) {
            continue;
        }
        callbacks.Queue(Msm8255ModemCallback{
            kWmsCbProg, kWmsCbVers, kWmsCbClientCid, kCbProcCfgEvent, c.self_pid, c.dest,
            {c.cfg_cb, kCfgEventMemoryStatusSet, kXdrTrue, kCfgEventMemoryStatusSet, w[3]}, {},
            0u, 0, c.id});
    }
}

WmsClient& Msm8255WmsRemoteServer::RequireClient(uint32_t proc, uint32_t id) {
    if (WmsClient* c = clients_.Find(id)) return *c;
    emu_.Get<Fatal>().Die(
        "msm8255 wms remote server: procedure %u names client %u, which this server never "
        "issued", proc, id);
}

void Msm8255WmsRemoteServer::SaveState(StateWriter& w) { clients_.Save(w, "wms"); }

void Msm8255WmsRemoteServer::RestoreState(StateReader& r) { clients_.Restore(r, "wms"); }

}

REGISTER_SERVICE(Msm8255WmsRemoteServer);

}
