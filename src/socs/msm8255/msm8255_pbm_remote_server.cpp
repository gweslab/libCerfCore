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

constexpr uint32_t kPbmProg = 0x30000012u;
constexpr uint32_t kPbmVers = 0x00040001u;
constexpr uint32_t kPbmCid  = 20u;

constexpr uint32_t kProcSimInitDone            = 3u;
constexpr uint32_t kProcNotificationRegistration   = 26u;
constexpr uint32_t kProcNotificationDeregistration = 27u;

constexpr uint32_t kStatusOk = 0u;

class Msm8255PbmRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255RpcServerRegistry>().Register(this); }

    uint32_t ServerProg() const override { return kPbmProg; }
    uint32_t ServerVers() const override { return kPbmVers; }
    uint32_t ServerCid() const override { return kPbmCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa, uint32_t out_cap,
                        uint32_t self_pid, uint32_t peer_pid, uint32_t peer_cid) override {
        auto& codec = emu_.Get<Msm8255OncrpcCodec>();
        const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
        const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
        const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kPbmCid, peer_pid, peer_cid,
                                       call.xid};
        uint32_t result = 0u;
        switch (call.proc) {
        case kProcNotificationRegistration:
            codec.RequireArgWords(*this, call.proc, w, 2u);
            LOG(SocModem, "msm8255 pbm remote server: notification callback %u, data 0x%08X\n",
                w[0], w[1]);
            result = kStatusOk;
            break;
        case kProcNotificationDeregistration:
            codec.RequireArgWords(*this, call.proc, w, 2u);
            LOG(SocModem, "msm8255 pbm remote server: notification callback %u deregistered, data "
                "0x%08X\n", w[0], w[1]);
            result = kStatusOk;
            break;
        case kProcSimInitDone:
            codec.RequireArgWords(*this, call.proc, w, 0u);
            result = kXdrFalse;
            break;
        default:
            emu_.Get<Fatal>().Die("msm8255 pbm remote server: procedure %u is not modeled",
                                  call.proc);
        }
        const uint32_t results[1] = {result};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
};

}

REGISTER_SERVICE(Msm8255PbmRemoteServer);

}
