#include "msm8255_oncrpc_codec.h"
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

namespace cerfcore {

namespace {

constexpr uint32_t kPdsmAtlProg = 0x3000001Du;
constexpr uint32_t kPdsmAtlVers = 0x00030001u;
constexpr uint32_t kPdsmAtlCid  = 13u;

constexpr uint32_t kProcL2ProxyReg   = 3u;
constexpr uint32_t kProcL2ProxyDereg = 4u;

constexpr uint32_t kRegPayloadBytes   = kPacmarkBytes + kCallArgsOff + 12u;
constexpr uint32_t kDeregPayloadBytes = kPacmarkBytes + kCallArgsOff + 4u;

constexpr uint32_t kProxyTypeOff = kCallArgsOff + 0u;
constexpr uint32_t kProxyType    = 1u;
constexpr uint32_t kResultWords  = 1u;

class Msm8255PdsmAtlRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { l2_proxy_registered_ = false; });
    }

    uint32_t ServerProg() const override { return kPdsmAtlProg; }
    uint32_t ServerVers() const override { return kPdsmAtlVers; }
    uint32_t ServerCid() const override { return kPdsmAtlCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa, uint32_t out_cap,
                        uint32_t self_pid, uint32_t peer_pid, uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        w.Write("pdsm_atl_l2_proxy_registered", l2_proxy_registered_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("pdsm_atl_l2_proxy_registered", l2_proxy_registered_);
    }

private:
    void RequireProxyType(uint32_t proc, uint32_t type);

    bool l2_proxy_registered_ = false;
};

void Msm8255PdsmAtlRemoteServer::RequireProxyType(uint32_t proc, uint32_t type) {
    if (type != kProxyType) {
        emu_.Get<Fatal>().Die(
            "msm8255 pdsm atl remote server: rpc procedure %u for proxy type %u is not modeled",
            proc, type);
    }
}

uint32_t Msm8255PdsmAtlRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                                                uint32_t out_cap, uint32_t self_pid,
                                                uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kPdsmAtlCid, peer_pid, peer_cid, call.xid};

    uint32_t result = kXdrFalse;
    if (call.proc == kProcL2ProxyReg) {
        codec.RequireCallBytes(*this, call.proc, size, kRegPayloadBytes);
        RequireProxyType(call.proc, codec.CallWord(call, kProxyTypeOff));
        l2_proxy_registered_ = true;
        result = kXdrTrue;
    } else if (call.proc == kProcL2ProxyDereg) {
        codec.RequireCallBytes(*this, call.proc, size, kDeregPayloadBytes);
        RequireProxyType(call.proc, codec.CallWord(call, kProxyTypeOff));
        result = l2_proxy_registered_ ? kXdrTrue : kXdrFalse;
        l2_proxy_registered_ = false;
    } else {
        emu_.Get<Fatal>().Die(
            "msm8255 pdsm atl remote server: rpc procedure %u with a %u-byte payload is not modeled",
            call.proc, size);
    }
    LOG(SocModem, "msm8255 pdsm atl remote server: procedure %u, answered %u\n", call.proc, result);
    const uint32_t results[kResultWords] = {result};
    return codec.WriteAcceptedReply(reply, results, kResultWords);
}

}

REGISTER_SERVICE(Msm8255PdsmAtlRemoteServer);

}
