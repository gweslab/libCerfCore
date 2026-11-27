#include "msm8255_oncrpc_codec.h"
#include "msm8255_pdapi_clients.h"
#include "msm8255_pdapi_engine.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kPdapiProg = 0x3000005Bu;
constexpr uint32_t kPdapiVers = 0x00100001u;
constexpr uint32_t kPdapiCid  = 12u;

constexpr uint32_t kProcClientInit    = 2u;
constexpr uint32_t kProcClientRelease = 3u;
constexpr uint32_t kProcPdReg         = 4u;
constexpr uint32_t kProcPaReg         = 5u;
constexpr uint32_t kProcLcsReg        = 6u;
constexpr uint32_t kProcXtraReg       = 7u;
constexpr uint32_t kProcExtReg        = 9u;
constexpr uint32_t kProcClientAct     = 10u;
constexpr uint32_t kProcClientDeact   = 11u;
constexpr uint32_t kProcGetPosition   = 12u;
constexpr uint32_t kProcEndSession    = 13u;
constexpr uint32_t kProcInjectPos     = 15u;
constexpr uint32_t kProcSetParam      = 17u;
constexpr uint32_t kProcGetParam      = 18u;
constexpr uint32_t kProcNiResponse    = 19u;
constexpr uint32_t kProcXtraSetData   = 28u;
constexpr uint32_t kProcXtraDownload  = 29u;
constexpr uint32_t kProcXtraAutoParam = 30u;
constexpr uint32_t kProcXtraValidity  = 31u;
constexpr uint32_t kProcXtraTime      = 32u;

constexpr uint32_t kClientTypePd   = 2u;
constexpr uint32_t kClientTypeLcs  = 4u;
constexpr uint32_t kClientTypeXtra = 11u;

constexpr uint32_t kEventReg     = 0u;
constexpr uint32_t kClientOk     = 0u;
constexpr uint32_t kClientFailed = 1u;
constexpr uint32_t kRegWords     = 6u;

constexpr uint32_t kXtraCmdAutoParam = 2u;
constexpr uint32_t kNiCmdResponse    = 0u;

class Msm8255PdapiRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255RpcServerRegistry>().Register(this); }

    uint32_t ServerProg() const override { return kPdapiProg; }
    uint32_t ServerVers() const override { return kPdapiVers; }
    uint32_t ServerCid() const override { return kPdapiCid; }

    void ReleaseClient(const Msm8255RpcEndpoint& client) override {
        emu_.Get<Msm8255PdapiClients>().ReleaseClient(client);
    }

    bool CallbackClientCid(uint32_t& cid) const override {
        cid = kPdapiCbClientCid;
        return true;
    }

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa, uint32_t out_cap,
                        uint32_t self_pid, uint32_t peer_pid, uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        emu_.Get<Msm8255PdapiClients>().SaveState(w);
        emu_.Get<Msm8255PdapiEngine>().SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        emu_.Get<Msm8255PdapiClients>().RestoreState(r);
        emu_.Get<Msm8255PdapiEngine>().RestoreState(r);
    }

private:
    std::vector<uint32_t> Args(const Msm8255OncrpcCall& call, uint32_t size) {
        return emu_.Get<Msm8255OncrpcCodec>().CallArgs(*this, call, size);
    }

    void RequireWords(uint32_t proc, const std::vector<uint32_t>& w, uint32_t want) {
        emu_.Get<Msm8255OncrpcCodec>().RequireArgWords(*this, proc, w, want);
    }

    void RequireAtLeast(uint32_t proc, const std::vector<uint32_t>& w, uint32_t want) {
        emu_.Get<Msm8255OncrpcCodec>().RequireArgWordsAtLeast(*this, proc, w, want);
    }

    Msm8255PdapiClient& RequireClient(uint32_t proc, uint32_t client) {
        Msm8255PdapiClient* c = emu_.Get<Msm8255PdapiClients>().Find(client);
        if (c == nullptr) {
            emu_.Get<Fatal>().Die(
                "msm8255 pdapi remote server: procedure %u names client 0x%08X, which this "
                "server never issued", proc, client);
        }
        return *c;
    }

    void RequireValue(uint32_t proc, const char* what, uint32_t value, uint32_t want) {
        if (value != want) {
            emu_.Get<Fatal>().Die(
                "msm8255 pdapi remote server: procedure %u carries %s %u, and only %u is modeled",
                proc, what, value, want);
        }
    }

    uint32_t InitClient(uint32_t type);
    uint32_t Register(uint32_t proc, Msm8255PdapiKind kind, const std::vector<uint32_t>& w,
                      const Msm8255PdapiCommand& route);
    uint32_t AnswerXtra(uint32_t proc, const std::vector<uint32_t>& w, Msm8255PdapiCommand cmd);
};

uint32_t Msm8255PdapiRemoteServer::InitClient(uint32_t type) {
    if (type != kClientTypePd && type != kClientTypeLcs && type != kClientTypeXtra) {
        emu_.Get<Fatal>().Die("msm8255 pdapi remote server: client init of type %u is not modeled",
                              type);
    }
    const uint32_t id = emu_.Get<Msm8255PdapiClients>().Create(type);
    LOG(SocModem, "msm8255 pdapi remote server: client init of type %u, client 0x%08X\n", type, id);
    return id;
}

uint32_t Msm8255PdapiRemoteServer::Register(uint32_t proc, Msm8255PdapiKind kind,
                                            const std::vector<uint32_t>& w,
                                            const Msm8255PdapiCommand& route) {
    RequireWords(proc, w, kRegWords);
    Msm8255PdapiClient& c = RequireClient(proc, w[0]);
    RequireValue(proc, "registration type", w[3], kEventReg);
    Msm8255PdapiRegistration& reg = c.regs[static_cast<uint32_t>(kind)];
    reg.set      = 1u;
    reg.data     = w[1];
    reg.callback = w[2];
    reg.mask     = w[4];
    reg.self_pid = route.self_pid;
    reg.dest     = route.source;
    return kClientOk;
}

uint32_t Msm8255PdapiRemoteServer::AnswerXtra(uint32_t proc, const std::vector<uint32_t>& w,
                                              Msm8255PdapiCommand cmd) {
    RequireAtLeast(proc, w, 3u);
    RequireClient(proc, w[1]);
    cmd.callback = w[0];
    cmd.data     = w[2];
    auto& engine = emu_.Get<Msm8255PdapiEngine>();
    switch (proc) {
    case kProcXtraSetData: {
        RequireAtLeast(proc, w, 8u);
        RequireValue(proc, "xtra part length", w[4], w[3]);
        RequireWords(proc, w, 5u + (w[4] + 3u) / 4u + 3u);
        engine.XtraDataPart(cmd, w[1]);
        break;
    }
    case kProcXtraDownload:
        RequireWords(proc, w, 3u);
        engine.XtraDownloadRequest(cmd, w[1]);
        break;
    case kProcXtraAutoParam:
        RequireWords(proc, w, 5u);
        engine.Acknowledge(cmd, kXtraCmdAutoParam);
        break;
    case kProcXtraValidity:
        RequireWords(proc, w, 3u);
        engine.XtraValidity(cmd, w[1]);
        break;
    default:
        RequireWords(proc, w, 9u);
        RequireValue(proc, "time info flag", w[3], kXdrTrue);
        engine.InjectTime(cmd, w[1]);
        break;
    }
    return kXdrTrue;
}

uint32_t Msm8255PdapiRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                                              uint32_t out_cap, uint32_t self_pid,
                                              uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec   = emu_.Get<Msm8255OncrpcCodec>();
    auto& clients = emu_.Get<Msm8255PdapiClients>();
    auto& engine  = emu_.Get<Msm8255PdapiEngine>();
    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kPdapiCid, peer_pid, peer_cid, call.xid};
    const std::vector<uint32_t> w = Args(call, size);
    Msm8255PdapiCommand cmd{kRpcNoCallback, 0u, self_pid, {peer_pid, peer_cid}};

    uint32_t result = kClientOk;
    switch (call.proc) {
    case kProcClientInit:
        RequireWords(call.proc, w, 1u);
        result = InitClient(w[0]);
        break;
    case kProcClientRelease:
        RequireWords(call.proc, w, 1u);
        result = clients.Remove(w[0]) ? kClientOk : kClientFailed;
        LOG(SocModem, "msm8255 pdapi remote server: release of client 0x%08X, answered %u\n", w[0],
            result);
        break;
    case kProcPdReg:   result = Register(call.proc, Msm8255PdapiKind::Pd, w, cmd); break;
    case kProcPaReg:   result = Register(call.proc, Msm8255PdapiKind::Pa, w, cmd); break;
    case kProcLcsReg:  result = Register(call.proc, Msm8255PdapiKind::Lcs, w, cmd); break;
    case kProcXtraReg: result = Register(call.proc, Msm8255PdapiKind::Xtra, w, cmd); break;
    case kProcExtReg:  result = Register(call.proc, Msm8255PdapiKind::Ext, w, cmd); break;
    case kProcClientAct:
    case kProcClientDeact:
        RequireWords(call.proc, w, 1u);
        RequireClient(call.proc, w[0]).active = call.proc == kProcClientAct ? 1u : 0u;
        break;
    case kProcGetPosition:
        RequireAtLeast(call.proc, w, 7u);
        RequireClient(call.proc, w.back());
        RequireValue(call.proc, "qos flag", w[w.size() - 4u], kXdrTrue);
        cmd.callback = w[0];
        cmd.data     = w[1];
        engine.StartSession(cmd, w.back(), w[w.size() - 2u]);
        result = kXdrTrue;
        break;
    case kProcEndSession:
        RequireWords(call.proc, w, 4u);
        RequireClient(call.proc, w[3]);
        cmd.callback = w[0];
        cmd.data     = w[2];
        engine.EndSession(cmd, w[3]);
        result = kXdrTrue;
        break;
    case kProcInjectPos:
        RequireAtLeast(call.proc, w, 4u);
        RequireClient(call.proc, w.back());
        RequireValue(call.proc, "position flag", w[2], kXdrTrue);
        cmd.callback = w[0];
        cmd.data     = w[1];
        engine.InjectPosition(cmd, w.back());
        result = kXdrTrue;
        break;
    case kProcSetParam:
        RequireAtLeast(call.proc, w, 7u);
        RequireClient(call.proc, w.back());
        RequireValue(call.proc, "parameter flag", w[3], kXdrTrue);
        RequireValue(call.proc, "parameter type echo", w[4], w[2]);
        cmd.callback = w[0];
        cmd.data     = w[1];
        engine.SetParam(cmd, w.back(), w[2], std::vector<uint32_t>(w.begin() + 5, w.end() - 1));
        result = kXdrTrue;
        break;
    case kProcGetParam:
        RequireWords(call.proc, w, 4u);
        RequireClient(call.proc, w[3]);
        cmd.callback = w[0];
        cmd.data     = w[1];
        engine.GetParam(cmd, w[3], w[2]);
        result = kXdrTrue;
        break;
    case kProcNiResponse:
        RequireAtLeast(call.proc, w, 4u);
        RequireClient(call.proc, w.back());
        cmd.callback = w[0];
        cmd.data     = w[1];
        engine.Acknowledge(cmd, kNiCmdResponse);
        result = kXdrTrue;
        break;
    case kProcXtraSetData:
    case kProcXtraDownload:
    case kProcXtraAutoParam:
    case kProcXtraValidity:
    case kProcXtraTime:
        result = AnswerXtra(call.proc, w, cmd);
        break;
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 pdapi remote server: rpc procedure %u with a %u-byte payload is not modeled",
            call.proc, size);
    }
    const uint32_t results[1] = {result};
    return codec.WriteAcceptedReply(reply, results, 1u);
}

}

REGISTER_SERVICE(Msm8255PdapiRemoteServer);

}
