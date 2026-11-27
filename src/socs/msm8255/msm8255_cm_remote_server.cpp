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

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kCmProg        = 0x30000000u;
constexpr uint32_t kCmVers        = 0x002D0001u;
constexpr uint32_t kCmCid         = 7u;
constexpr uint32_t kCmCbProg      = 0x31000000u;
constexpr uint32_t kCmCbVers      = 0x002D0002u;
constexpr uint32_t kCmCbClientCid = 15u;

constexpr uint32_t kProcPhInfoGet    = 36u;
constexpr uint32_t kProcSetDeltaFilters = 39u;
constexpr uint32_t kProcSsInfoGet    = 61u;
constexpr uint32_t kProcClientInit   = 99u;
constexpr uint32_t kProcClientRelease = 100u;
constexpr uint32_t kProcCallReg      = 101u;
constexpr uint32_t kProcPhReg        = 103u;
constexpr uint32_t kProcSsReg        = 105u;
constexpr uint32_t kProcInbandReg    = 107u;
constexpr uint32_t kProcSupsReg      = 115u;
constexpr uint32_t kProcClientAct    = 120u;
constexpr uint32_t kProcClientDeact  = 121u;
constexpr uint32_t kProcStatsSetMask = 171u;
constexpr uint32_t kProcStatsReg     = 172u;

constexpr uint32_t kStatsModuleGsm   = 1u;
constexpr uint32_t kStatsModuleUmts  = 2u;
constexpr uint32_t kStatsModuleCount = 2u;
constexpr uint32_t kDeltaFilterCount = 5u;

constexpr uint32_t kCbProcCmd     = 2u;
constexpr uint32_t kCbProcPhEvent = 17u;
constexpr uint32_t kCbProcSsEvent = 20u;

constexpr uint32_t kClientOk        = 0u;
constexpr uint32_t kRegTypeReg      = 0u;
constexpr uint32_t kRegTypeDereg    = 1u;
constexpr uint32_t kPhEventInfo     = 12u;
constexpr uint32_t kSsEventInfo     = 2u;
constexpr uint32_t kCmdNoErr        = 0u;
constexpr uint32_t kPhInfoCmd       = 0u;
constexpr uint32_t kSsInfoCmd       = 0u;
constexpr uint32_t kStatsSetMaskCmd = 0u;
constexpr uint32_t kDeltaFiltersCmd = 0u;

enum CmEventClass : uint32_t {
    kEventsPh,
    kEventsCall,
    kEventsSs,
    kEventsInband,
    kEventsSups,
    kEventsStats,
    kEventClassCount
};

constexpr uint32_t kEventWords  = 2u;
constexpr uint32_t kEventsStored = kEventWords * 64u;

constexpr uint32_t kPhInfoWords          = 189u;
constexpr uint32_t kPhInfoOprtModeWord   = 1u;
constexpr uint32_t kPhInfoModePrefWord   = 5u;
constexpr uint32_t kPhInfoNetSelWord     = 18u;
constexpr uint32_t kPhInfoPlmnWord       = 19u;
constexpr uint32_t kPhInfoModeCapWord    = 179u;
constexpr uint32_t kOprtModeLpm          = 6u;
constexpr uint32_t kModePrefAutomatic    = 2u;
constexpr uint32_t kNetSelAutomatic      = 0u;
constexpr uint32_t kPlmnNone             = 0u;
constexpr uint32_t kModeCapGsmWcdma      = 0x28u;

constexpr uint32_t kSsInfoWords = 233u;

struct CmClient {
    uint64_t           events[kEventClassCount * kEventWords] = {};
    uint64_t           stats_masks[kStatsModuleCount]        = {};
    uint32_t           id                                    = 0u;
    uint32_t           active                                = 0u;
    uint32_t           event_cb[kEventClassCount]            = {};
    uint32_t           error_cb[kEventClassCount]            = {};
    uint32_t           delta_filters[kDeltaFilterCount]      = {};
    uint32_t           self_pid                              = 0u;
    Msm8255RpcEndpoint dest;

    template <typename F>
    static constexpr void Visit(CmClient& c, F& field) {
        field("cm_client_id", c.id);
        field("cm_client_active", c.active);
        field("cm_client_events", c.events);
        field("cm_client_event_cb", c.event_cb);
        field("cm_client_error_cb", c.error_cb);
        field("cm_client_stats_masks", c.stats_masks);
        field("cm_client_delta_filters", c.delta_filters);
        field("cm_client_self_pid", c.self_pid);
        field("cm_client_dest_pid", c.dest.pid);
        field("cm_client_dest_cid", c.dest.cid);
    }
};

class Msm8255CmRemoteServer : public Msm8255RpcServer {
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

    uint32_t ServerProg() const override { return kCmProg; }
    uint32_t ServerVers() const override { return kCmVers; }
    uint32_t ServerCid() const override { return kCmCid; }

    void ReleaseClient(const Msm8255RpcEndpoint& client) override {
        for (CmClient& c : clients_.Clients()) {
            if (c.dest != client) continue;
            std::fill(std::begin(c.event_cb), std::end(c.event_cb), kRpcNoCallback);
            std::fill(std::begin(c.error_cb), std::end(c.error_cb), kRpcNoCallback);
        }
    }

    bool CallbackClientCid(uint32_t& cid) const override {
        cid = kCmCbClientCid;
        return true;
    }

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    CmClient& RequireClient(uint32_t proc, uint32_t id) {
        if (CmClient* c = clients_.Find(id)) return *c;
        emu_.Get<Fatal>().Die(
            "msm8255 cm remote server: procedure %u names client 0x%08X, which this server never "
            "issued", proc, id);
    }

    void Register(CmEventClass cls, uint32_t proc, const std::vector<uint32_t>& w,
                  uint32_t self_pid, const Msm8255RpcEndpoint& source);
    void QueueCommandCallback(uint32_t cmd_cb, uint32_t data, uint32_t cmd, uint32_t self_pid,
                              const Msm8255RpcEndpoint& source);
    void QueuePhInfo(const CmClient& c, uint32_t cmd_cb, uint32_t data, uint32_t self_pid,
                     const Msm8255RpcEndpoint& source);
    void QueueInfoEvent(const CmClient& c, CmEventClass cls, uint32_t cb_proc, uint32_t event,
                        const std::vector<uint32_t>& info);

    Msm8255RpcClientTable<CmClient> clients_{UINT32_MAX};
};

uint32_t Msm8255CmRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                                           uint32_t out_cap, uint32_t self_pid,
                                           uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
    const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kCmCid, peer_pid, peer_cid, call.xid};
    const Msm8255RpcEndpoint source{peer_pid, peer_cid};

    switch (call.proc) {
    case kProcClientInit: {
        codec.RequireArgWords(*this, call.proc, w, 2u);
        codec.RequireBoolean(*this, call.proc, w[1]);
        CmClient* c = clients_.Issue();
        if (!c) {
            emu_.Get<Fatal>().Die("msm8255 cm remote server: client init after %u issued ids",
                                  clients_.LastId());
        }
        c->self_pid = self_pid;
        c->dest     = source;
        LOG(SocModem, "msm8255 cm remote server: client init of type %u, client 0x%08X\n", w[0],
            c->id);
        return codec.WriteOptionalReply(reply, kClientOk, w[1], c->id);
    }
    case kProcClientRelease: {
        codec.RequireArgWords(*this, call.proc, w, 1u);
        RequireClient(call.proc, w[0]);
        clients_.Remove(w[0]);
        break;
    }
    case kProcPhReg:     Register(kEventsPh, call.proc, w, self_pid, source); break;
    case kProcCallReg:   Register(kEventsCall, call.proc, w, self_pid, source); break;
    case kProcSsReg:     Register(kEventsSs, call.proc, w, self_pid, source); break;
    case kProcInbandReg: Register(kEventsInband, call.proc, w, self_pid, source); break;
    case kProcSupsReg:   Register(kEventsSups, call.proc, w, self_pid, source); break;
    case kProcStatsReg:  Register(kEventsStats, call.proc, w, self_pid, source); break;
    case kProcStatsSetMask: {
        codec.RequireArgWords(*this, call.proc, w, 6u);
        CmClient& c = RequireClient(call.proc, w[2]);
        if (w[3] != kStatsModuleGsm && w[3] != kStatsModuleUmts) {
            emu_.Get<Fatal>().Die(
                "msm8255 cm remote server: modem info mask 0x%08X%08X for stats module %u is not "
                "modeled", w[4], w[5], w[3]);
        }
        c.stats_masks[w[3] - kStatsModuleGsm] = (static_cast<uint64_t>(w[4]) << 32) | w[5];
        QueueCommandCallback(w[0], w[1], kStatsSetMaskCmd, self_pid, source);
        const uint32_t results[1] = {kXdrTrue};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
    case kProcSetDeltaFilters: {
        codec.RequireArgWords(*this, call.proc, w, 3u + kDeltaFilterCount);
        CmClient& c = RequireClient(call.proc, w[2]);
        for (uint32_t i = 0; i < kDeltaFilterCount; ++i) c.delta_filters[i] = w[3u + i];
        QueueCommandCallback(w[0], w[1], kDeltaFiltersCmd, self_pid, source);
        const uint32_t results[1] = {kXdrTrue};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
    case kProcClientAct:
    case kProcClientDeact: {
        codec.RequireArgWords(*this, call.proc, w, 1u);
        RequireClient(call.proc, w[0]).active = call.proc == kProcClientAct ? 1u : 0u;
        break;
    }
    case kProcPhInfoGet: {
        codec.RequireArgWords(*this, call.proc, w, 3u);
        QueuePhInfo(RequireClient(call.proc, w[2]), w[0], w[1], self_pid, source);
        const uint32_t results[1] = {kXdrTrue};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
    case kProcSsInfoGet: {
        codec.RequireArgWords(*this, call.proc, w, 3u);
        const CmClient& c = RequireClient(call.proc, w[2]);
        QueueCommandCallback(w[0], w[1], kSsInfoCmd, self_pid, source);
        LOG(SocModem, "msm8255 cm remote server: serving system info for client 0x%08X: no "
                      "service\n", c.id);
        QueueInfoEvent(c, kEventsSs, kCbProcSsEvent, kSsEventInfo,
                       std::vector<uint32_t>(kSsInfoWords, 0u));
        const uint32_t results[1] = {kXdrTrue};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
    default:
        emu_.Get<Fatal>().Die("msm8255 cm remote server: procedure %u is not modeled", call.proc);
    }
    const uint32_t results[1] = {kClientOk};
    return codec.WriteAcceptedReply(reply, results, 1u);
}

void Msm8255CmRemoteServer::Register(CmEventClass cls, uint32_t proc,
                                     const std::vector<uint32_t>& w, uint32_t self_pid,
                                     const Msm8255RpcEndpoint& source) {
    emu_.Get<Msm8255OncrpcCodec>().RequireArgWords(*this, proc, w, 6u);
    CmClient& c = RequireClient(proc, w[0]);
    if ((w[2] != kRegTypeReg && w[2] != kRegTypeDereg) || w[3] > w[4] || w[4] >= kEventsStored) {
        emu_.Get<Fatal>().Die(
            "msm8255 cm remote server: procedure %u registration type %u for events %u..%u "
            "is not modeled", proc, w[2], w[3], w[4]);
    }
    const bool reg = w[2] == kRegTypeReg;
    for (uint32_t e = w[3]; e <= w[4]; ++e) {
        uint64_t&      word = c.events[cls * kEventWords + e / 64u];
        const uint64_t bit  = 1ull << (e % 64u);
        word = reg ? (word | bit) : (word & ~bit);
    }
    if (!reg) return;
    c.event_cb[cls] = w[1];
    c.error_cb[cls] = w[5];
    c.self_pid      = self_pid;
    c.dest          = source;
}

void Msm8255CmRemoteServer::QueueCommandCallback(uint32_t cmd_cb, uint32_t data, uint32_t cmd,
                                                 uint32_t self_pid,
                                                 const Msm8255RpcEndpoint& source) {
    if (cmd_cb == kRpcNoCallback) return;
    emu_.Get<Msm8255ModemCallbacks>().Queue(
        Msm8255ModemCallback{kCmCbProg, kCmCbVers, kCmCbClientCid, kCbProcCmd, self_pid, source,
                             {cmd_cb, data, cmd, kCmdNoErr}, {}, 0u, 0});
}

void Msm8255CmRemoteServer::QueuePhInfo(const CmClient& c, uint32_t cmd_cb, uint32_t data,
                                        uint32_t self_pid, const Msm8255RpcEndpoint& source) {
    QueueCommandCallback(cmd_cb, data, kPhInfoCmd, self_pid, source);
    std::vector<uint32_t> info(kPhInfoWords, 0u);
    info[kPhInfoOprtModeWord] = kOprtModeLpm;
    info[kPhInfoModePrefWord] = kModePrefAutomatic;
    info[kPhInfoNetSelWord]   = kNetSelAutomatic;
    info[kPhInfoPlmnWord]     = kPlmnNone;
    info[kPhInfoModeCapWord]  = kModeCapGsmWcdma;
    LOG(SocModem,
        "msm8255 cm remote server: phone info for client 0x%08X: operating mode %u, mode pref %u, "
        "network selection %u, capability 0x%02X\n",
        c.id, kOprtModeLpm, kModePrefAutomatic, kNetSelAutomatic, kModeCapGsmWcdma);
    QueueInfoEvent(c, kEventsPh, kCbProcPhEvent, kPhEventInfo, info);
}

void Msm8255CmRemoteServer::QueueInfoEvent(const CmClient& c, CmEventClass cls, uint32_t cb_proc,
                                           uint32_t event, const std::vector<uint32_t>& info) {
    if (c.active == 0u || c.event_cb[cls] == kRpcNoCallback ||
        (c.events[cls * kEventWords + event / 64u] & (1ull << (event % 64u))) == 0u) {
        return;
    }
    std::vector<uint32_t> args = {c.event_cb[cls], event, kXdrTrue};
    args.insert(args.end(), info.begin(), info.end());
    emu_.Get<Msm8255ModemCallbacks>().Queue(Msm8255ModemCallback{
        kCmCbProg, kCmCbVers, kCmCbClientCid, cb_proc, c.self_pid, c.dest, std::move(args), {},
        0u, 0});
}

void Msm8255CmRemoteServer::SaveState(StateWriter& w) {
    clients_.Save(w, "cm");
}

void Msm8255CmRemoteServer::RestoreState(StateReader& r) {
    clients_.Restore(r, "cm");
}

}

REGISTER_SERVICE(Msm8255CmRemoteServer);

}
