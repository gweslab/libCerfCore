#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kPmappProg = 0x30000060u;
constexpr uint32_t kPmappVers = 0x00050001u;
constexpr uint32_t kPmappCid  = 5u;

constexpr uint32_t kProcVregSwitch         = 3u;
constexpr uint32_t kProcVregQuery          = 4u;
constexpr uint32_t kProcDisplayClockConfig = 21u;

constexpr uint32_t kSwitchPayloadBytes      = kPacmarkBytes + kCallArgsOff + 12u;
constexpr uint32_t kQueryPayloadBytes       = kPacmarkBytes + kCallArgsOff + 8u;
constexpr uint32_t kDisplayClockPayloadBytes = kPacmarkBytes + kCallArgsOff + 4u;

constexpr uint32_t kSwitchResultWords       = 0u;
constexpr uint32_t kDisplayClockResultWords = 1u;

constexpr uint32_t kSwitchCmdOff   = kCallArgsOff + 0u;
constexpr uint32_t kSwitchVregOff  = kCallArgsOff + 4u;
constexpr uint32_t kSwitchDatumOff = kCallArgsOff + 8u;

constexpr uint32_t kQueryVregOff      = kCallArgsOff + 0u;
constexpr uint32_t kQueryWantDatumOff = kCallArgsOff + 4u;

/* Linux arch/arm/mach-msm rpc_pmapp.c: modem_to_linux_err answers success for a
   zero reply word and maps every PM_ERR_FLAG_* bit to a failure. */
constexpr uint32_t kErrFlagNone = 0u;

class Msm8255PmappRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { token_vregs_.clear(); });
    }

    uint32_t ServerProg() const override { return kPmappProg; }
    uint32_t ServerVers() const override { return kPmappVers; }
    uint32_t ServerCid() const override { return kPmappCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        w.Write("pmapp_token_vreg_count", static_cast<uint32_t>(token_vregs_.size()));
        w.WriteBytes("pmapp_token_vregs", token_vregs_.data(),
                     token_vregs_.size() * sizeof(uint32_t));
    }

    void RestoreState(StateReader& r) override {
        uint32_t count = 0u;
        r.Read("pmapp_token_vreg_count", count);
        token_vregs_.resize(count);
        r.ReadBytes("pmapp_token_vregs", token_vregs_.data(),
                    token_vregs_.size() * sizeof(uint32_t));
    }

private:
    static uint32_t TokenFor(uint32_t vreg_id) { return vreg_id + 1u; }

    bool TokenIssued(uint32_t vreg_id) const {
        return std::find(token_vregs_.begin(), token_vregs_.end(), vreg_id) != token_vregs_.end();
    }

    void RequireVregSwitchDatum(uint32_t vreg_id, uint32_t datum);

    std::vector<uint32_t> token_vregs_;
};

void Msm8255PmappRemoteServer::RequireVregSwitchDatum(uint32_t vreg_id, uint32_t datum) {
    if (!TokenIssued(vreg_id) || datum != TokenFor(vreg_id)) {
        emu_.Get<Fatal>().Die(
            "msm8255 pmapp remote server: vreg %u switch carries datum "
            "0x%08X, which this server never supplied through its query call",
            vreg_id, datum);
    }
}

uint32_t Msm8255PmappRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                              uint32_t out_pa, uint32_t out_cap,
                                              uint32_t self_pid,
                                              uint32_t peer_pid,
                                              uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kPmappCid, peer_pid, peer_cid, call.xid};

    if (call.proc == kProcVregSwitch) {
        codec.RequireCallBytes(*this, call.proc, size, kSwitchPayloadBytes);
        codec.RequireBoolean(*this, call.proc, codec.CallWord(call, kSwitchCmdOff));
        RequireVregSwitchDatum(codec.CallWord(call, kSwitchVregOff),
                               codec.CallWord(call, kSwitchDatumOff));
        return codec.WriteAcceptedReply(reply, nullptr, kSwitchResultWords);
    }

    if (call.proc == kProcDisplayClockConfig) {
        codec.RequireCallBytes(*this, call.proc, size,
                               kDisplayClockPayloadBytes);
        const uint32_t results[kDisplayClockResultWords] = {kErrFlagNone};
        return codec.WriteAcceptedReply(reply, results, kDisplayClockResultWords);
    }

    if (call.proc != kProcVregQuery) {
        emu_.Get<Fatal>().Die(
            "msm8255 pmapp remote server: rpc procedure %u with a %u-byte "
            "payload is not modeled", call.proc, size);
    }
    codec.RequireCallBytes(*this, call.proc, size, kQueryPayloadBytes);
    const uint32_t vreg_id    = codec.CallWord(call, kQueryVregOff);
    const uint32_t want_datum = codec.CallWord(call, kQueryWantDatumOff);
    codec.RequireBoolean(*this, call.proc, want_datum);
    if (want_datum == kXdrTrue && !TokenIssued(vreg_id)) token_vregs_.push_back(vreg_id);
    return codec.WriteOptionalReply(reply, kXdrTrue, want_datum, TokenFor(vreg_id));
}

}

REGISTER_SERVICE(Msm8255PmappRemoteServer);

}
