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

constexpr uint32_t kGstkProg = 0x3000003Cu;
constexpr uint32_t kGstkVers = 0x00090001u;
constexpr uint32_t kGstkCid  = 21u;

constexpr uint32_t kProcClientInit        = 2u;
constexpr uint32_t kProcClientEventReg    = 5u;
constexpr uint32_t kProcToolkitCommandReg = 6u;
constexpr uint32_t kProcUpdateProfileDownload = 17u;

constexpr uint32_t kStatusOk          = 0u;
constexpr uint32_t kEventRegWords     = 5u;
constexpr uint32_t kRegHeaderWords    = 4u;
constexpr uint32_t kRegEntryWords     = 4u;
constexpr uint32_t kRegCallbackWords  = 2u;
constexpr uint32_t kProfileHeaderWords = 4u;
constexpr uint32_t kProfileCountWord   = 3u;
constexpr uint32_t kProfileEntryWords  = 3u;

class Msm8255GstkRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            clients_.Clear();
            profile_.clear();
        });
    }

    uint32_t ServerProg() const override { return kGstkProg; }
    uint32_t ServerVers() const override { return kGstkVers; }
    uint32_t ServerCid() const override { return kGstkCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    uint32_t AnswerClientInit(const Msm8255OncrpcCall& call, const std::vector<uint32_t>& w,
                              const Msm8255OncrpcRoute& reply);
    void CheckToolkitCommandReg(const Msm8255OncrpcCall& call, const std::vector<uint32_t>& w);
    void CheckEventDeregistration(const Msm8255OncrpcCall& call, const std::vector<uint32_t>& w);
    void StoreProfileDownload(const Msm8255OncrpcCall& call, const std::vector<uint32_t>& w);
    void RequireIssuedClient(const char* request, uint32_t client);

    Msm8255RpcClientTable<Msm8255RpcClientId> clients_{UINT32_MAX};
    std::vector<uint32_t> profile_;
};

uint32_t Msm8255GstkRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                                             uint32_t out_cap, uint32_t self_pid,
                                             uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);
    const std::vector<uint32_t> w = codec.CallArgs(*this, call, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kGstkCid, peer_pid, peer_cid,
                                   call.xid};
    switch (call.proc) {
    case kProcClientInit:
        return AnswerClientInit(call, w, reply);
    case kProcClientEventReg:
        CheckEventDeregistration(call, w);
        break;
    case kProcToolkitCommandReg:
        CheckToolkitCommandReg(call, w);
        break;
    case kProcUpdateProfileDownload:
        StoreProfileDownload(call, w);
        break;
    default:
        emu_.Get<Fatal>().Die("msm8255 gstk remote server: procedure %u is not modeled",
                              call.proc);
    }
    const uint32_t results[1] = {kStatusOk};
    return codec.WriteAcceptedReply(reply, results, 1u);
}

uint32_t Msm8255GstkRemoteServer::AnswerClientInit(const Msm8255OncrpcCall& call,
                                                   const std::vector<uint32_t>& w,
                                                   const Msm8255OncrpcRoute& reply) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWords(*this, call.proc, w, 2u);
    codec.RequireBoolean(*this, call.proc, w[0]);
    const Msm8255RpcClientId* client = clients_.Issue();
    if (!client) {
        emu_.Get<Fatal>().Die("msm8255 gstk remote server: client init after %u issued ids",
                              clients_.LastId());
    }
    LOG(SocModem, "msm8255 gstk remote server: client init of type %u, client %u\n", w[1],
        client->id);
    return codec.WriteOptionalReply(reply, kStatusOk, w[0], client->id);
}

void Msm8255GstkRemoteServer::CheckToolkitCommandReg(const Msm8255OncrpcCall& call,
                                                     const std::vector<uint32_t>& w) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWordsAtLeast(*this, call.proc, w, kRegHeaderWords + 1u + kRegCallbackWords);
    RequireIssuedClient("toolkit command registration", w[0]);
    const uint32_t entries = w[kRegHeaderWords];
    const uint64_t want = static_cast<uint64_t>(kRegHeaderWords) + 1u +
                          static_cast<uint64_t>(entries) * kRegEntryWords + kRegCallbackWords;
    if (w.size() != want) {
        emu_.Get<Fatal>().Die(
            "msm8255 gstk remote server: toolkit command registration with %u entries in %u "
            "argument words", entries, static_cast<uint32_t>(w.size()));
    }
    LOG(SocModem, "msm8255 gstk remote server: client %u toolkit command registration, %u "
        "entries\n", w[0], entries);
}

void Msm8255GstkRemoteServer::CheckEventDeregistration(const Msm8255OncrpcCall& call,
                                                       const std::vector<uint32_t>& w) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWords(*this, call.proc, w, kEventRegWords);
    RequireIssuedClient("event registration", w[0]);
    codec.RequireNoCallback(*this, call.proc, w[2]);
    if (w[3] != 0u || w[4] != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 gstk remote server: client %u event registration with class mask 0x%08X%08X",
            w[0], w[3], w[4]);
    }
    LOG(SocModem, "msm8255 gstk remote server: client %u event deregistration\n", w[0]);
}

void Msm8255GstkRemoteServer::StoreProfileDownload(const Msm8255OncrpcCall& call,
                                                   const std::vector<uint32_t>& w) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWordsAtLeast(*this, call.proc, w, kProfileHeaderWords);
    RequireIssuedClient("profile download update", w[0]);
    const uint32_t entries = w[kProfileCountWord];
    const uint64_t want = kProfileHeaderWords +
                          static_cast<uint64_t>(entries) * kProfileEntryWords;
    if (w.size() != want) {
        emu_.Get<Fatal>().Die(
            "msm8255 gstk remote server: profile download update with %u entries in %u "
            "argument words", entries, static_cast<uint32_t>(w.size()));
    }
    profile_.assign(w.begin() + kProfileHeaderWords, w.end());
    LOG(SocModem, "msm8255 gstk remote server: client %u profile download update, %u entries\n",
        w[0], entries);
}

void Msm8255GstkRemoteServer::RequireIssuedClient(const char* request, uint32_t client) {
    if (clients_.Find(client)) return;
    emu_.Get<Fatal>().Die(
        "msm8255 gstk remote server: %s names client %u, which this server never issued",
        request, client);
}

void Msm8255GstkRemoteServer::SaveState(StateWriter& w) {
    clients_.Save(w, "gstk");
    w.Write<uint32_t>("gstk_profile_words", static_cast<uint32_t>(profile_.size()));
    w.WriteBytes("gstk_profile", profile_.data(), profile_.size() * sizeof(uint32_t));
}

void Msm8255GstkRemoteServer::RestoreState(StateReader& r) {
    clients_.Restore(r, "gstk");
    uint32_t words = 0;
    r.Read("gstk_profile_words", words);
    profile_.assign(words, 0u);
    r.ReadBytes("gstk_profile", profile_.data(), profile_.size() * sizeof(uint32_t));
}

}

REGISTER_SERVICE(Msm8255GstkRemoteServer);

}
