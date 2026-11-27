#include "msm8255_oncrpc_codec.h"
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

constexpr uint32_t kGsdiExpProg = 0x30000016u;
constexpr uint32_t kGsdiExpVers = 0x00040001u;
constexpr uint32_t kGsdiExpCid  = 19u;

constexpr uint32_t kProcPersoEventRegistration = 31u;
constexpr uint32_t kStatusOk                   = 0u;

class Msm8255GsdiExpRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255RpcServerRegistry>().Register(this); }

    uint32_t ServerProg() const override { return kGsdiExpProg; }
    uint32_t ServerVers() const override { return kGsdiExpVers; }
    uint32_t ServerCid() const override { return kGsdiExpCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa, uint32_t out_cap,
                        uint32_t self_pid, uint32_t peer_pid, uint32_t peer_cid) override {
        auto& codec = emu_.Get<Msm8255OncrpcCodec>();
        const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
        const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
        const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kGsdiExpCid, peer_pid, peer_cid,
                                       call.xid};
        if (call.proc != kProcPersoEventRegistration) {
            emu_.Get<Fatal>().Die("msm8255 gsdi exp remote server: procedure %u is not modeled",
                                  call.proc);
        }
        codec.RequireArgWords(*this, call.proc, w, 3u);
        LOG(SocModem, "msm8255 gsdi exp remote server: perso event callback %u, data 0x%08X, "
            "command callback %u\n", w[0], w[1], w[2]);
        const uint32_t results[1] = {kStatusOk};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
};

}

REGISTER_SERVICE(Msm8255GsdiExpRemoteServer);

}
