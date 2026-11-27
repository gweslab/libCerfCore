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

constexpr uint32_t kRemoteStorageProg = 0x300000A7u;
constexpr uint32_t kRemoteStorageVers = 0x00010001u;
constexpr uint32_t kRemoteStorageCid  = 25u;

constexpr uint32_t kProcForceSync     = 7u;
constexpr uint32_t kProcGetSyncStatus = 8u;

constexpr uint32_t kSyncToken    = 1u;
constexpr uint32_t kSyncComplete = 1u;

class Msm8255RemoteStorageRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255RpcServerRegistry>().Register(this); }

    uint32_t ServerProg() const override { return kRemoteStorageProg; }
    uint32_t ServerVers() const override { return kRemoteStorageVers; }
    uint32_t ServerCid() const override { return kRemoteStorageCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa, uint32_t out_cap,
                        uint32_t self_pid, uint32_t peer_pid, uint32_t peer_cid) override {
        auto& codec = emu_.Get<Msm8255OncrpcCodec>();
        const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
        const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
        const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kRemoteStorageCid, peer_pid, peer_cid,
                                       call.xid};
        uint32_t result = 0;
        switch (call.proc) {
        case kProcForceSync:
            codec.RequireArgWords(*this, call.proc, w, 0u);
            LOG(SocModem, "msm8255 remote storage remote server: force sync, token %u\n", kSyncToken);
            result = kSyncToken;
            break;
        case kProcGetSyncStatus:
            codec.RequireArgWords(*this, call.proc, w, 1u);
            if (w[0] != kSyncToken) {
                emu_.Get<Fatal>().Die(
                    "msm8255 remote storage remote server: sync status for token %u, issued token %u",
                    w[0], kSyncToken);
            }
            LOG(SocModem, "msm8255 remote storage remote server: sync status for token %u complete\n",
                w[0]);
            result = kSyncComplete;
            break;
        default:
            emu_.Get<Fatal>().Die(
                "msm8255 remote storage remote server: procedure %u with a %u-byte payload is not modeled",
                call.proc, size);
        }
        const uint32_t results[1] = {result};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
};

}

REGISTER_SERVICE(Msm8255RemoteStorageRemoteServer);

}
