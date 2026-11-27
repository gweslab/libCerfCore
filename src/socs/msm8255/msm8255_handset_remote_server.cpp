#include "msm8255_handset_remote_server.h"

#include "msm8255_modem_callbacks.h"
#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"
#include "msm8255_system_power.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kHandsetProg = 0x30000091u;
constexpr uint32_t kHandsetVers = 0x00030001u;
constexpr uint32_t kHandsetCid  = 23u;

constexpr uint32_t kHandsetCbProg      = 0x31000091u;
constexpr uint32_t kHandsetCbVers      = 0x00030005u;
constexpr uint32_t kHandsetCbClientCid = 24u;
constexpr uint32_t kHandsetCbProcEvent = 1u;

constexpr uint32_t kProcProcessCmd      = 2u;
constexpr uint32_t kProcSubscribeSrvc   = 3u;
constexpr uint32_t kProcPolledUntilTrue = 4u;

constexpr uint32_t kHsTrue = 1u;

constexpr uint32_t kPolledArgWords = 4u;

constexpr uint32_t kSubsArgWords = 10u;
constexpr uint32_t kSubsVer      = 1u;
constexpr uint32_t kSrvcSendCmd  = 0u;
constexpr uint32_t kSrvcRcvEvnt  = 1u;
constexpr uint32_t kSrvcCount    = 2u;
constexpr uint32_t kReqRegister  = 0u;
constexpr uint32_t kCmdClassLast = 2u;
constexpr uint32_t kEvntClassAll = 0u;

constexpr uint32_t kCmdArgWords          = 11u;
constexpr uint32_t kCmdVer               = 3u;
constexpr uint32_t kCmdSetPwrKeyRstThold = 7u;
constexpr uint32_t kCmdSetPwrKeyThold    = 8u;

constexpr uint32_t kRstHoldMsMask  = 0x0000FFFFu;
constexpr uint32_t kRstPowerOffBit = 1u << 16;
constexpr uint32_t kRstHwTimerBit  = 1u << 17;

constexpr uint32_t kHsPwrKey   = 0x6Fu;
constexpr uint32_t kHsRelKey   = 0xFFu;
constexpr uint32_t kKeyDataVer = 1u;

}

REGISTER_SERVICE(Msm8255HandsetRemoteServer);

bool Msm8255HandsetRemoteServer::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255HandsetRemoteServer::OnReady() {
    emu_.Get<Msm8255RpcServerRegistry>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        subscribers_[kSrvcSendCmd] = Subscriber{};
        subscribers_[kSrvcRcvEvnt] = Subscriber{};
    });
}

uint32_t Msm8255HandsetRemoteServer::ServerProg() const { return kHandsetProg; }
uint32_t Msm8255HandsetRemoteServer::ServerVers() const { return kHandsetVers; }
uint32_t Msm8255HandsetRemoteServer::ServerCid() const { return kHandsetCid; }

bool Msm8255HandsetRemoteServer::CallbackClientCid(uint32_t& cid) const {
    cid = kHandsetCbClientCid;
    return true;
}

void Msm8255HandsetRemoteServer::ReleaseClient(const Msm8255RpcEndpoint& client) {
    for (Subscriber& s : subscribers_) {
        if (s.in_use != 0u && Msm8255RpcEndpoint{s.peer_pid, s.peer_cid} == client) s = Subscriber{};
    }
}

uint32_t Msm8255HandsetRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                                uint32_t out_pa, uint32_t out_cap,
                                                uint32_t self_pid, uint32_t peer_pid,
                                                uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kHandsetCid, peer_pid, peer_cid, call.xid};
    const std::vector<uint32_t> args = codec.CallArgs(*this, call, size);
    uint32_t result = kHsTrue;
    switch (call.proc) {
    case kProcPolledUntilTrue:
        codec.RequireArgWords(*this, call.proc, args, kPolledArgWords);
        if (args[0] != kXdrTrue) {
            emu_.Get<Fatal>().Die(
                "msm8255 handset remote server: procedure %u with argument presence word %u "
                "is not modeled", call.proc, args[0]);
        }
        break;
    case kProcSubscribeSrvc:
        result = AnswerSubscribe(call.proc, args, reply);
        break;
    case kProcProcessCmd:
        result = AnswerCommand(call.proc, args);
        break;
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: rpc procedure %u with a %u-byte payload is not "
            "modeled", call.proc, size);
    }
    const uint32_t results[1] = {result};
    return codec.WriteAcceptedReply(reply, results, 1u);
}

uint32_t Msm8255HandsetRemoteServer::AnswerSubscribe(uint32_t proc,
                                                     const std::vector<uint32_t>& args,
                                                     const Msm8255OncrpcRoute& reply) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWords(*this, proc, args, kSubsArgWords);
    const uint32_t present = args[0], ver = args[1], srvc = args[2], req = args[3];
    const uint32_t host_os = args[4], disc = args[5], id = args[6];
    const uint32_t callback = args[7], handle_present = args[8], handle = args[9];
    const bool class_ok = srvc == kSrvcSendCmd ? id < kCmdClassLast : id == kEvntClassAll;
    if (present != kXdrTrue || ver != kSubsVer || srvc >= kSrvcCount || req != kReqRegister ||
        disc != srvc || !class_ok || handle_present != kXdrTrue) {
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: subscribe present %u ver %u srvc %u req %u disc %u "
            "id %u handle present %u is not modeled", present, ver, srvc, req, disc, id,
            handle_present);
    }
    if (srvc == kSrvcSendCmd) codec.RequireNoCallback(*this, proc, callback);
    if (srvc == kSrvcRcvEvnt && callback == kRpcNoCallback) {
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: an event subscription with no callback is not "
            "modeled");
    }
    Subscriber& s = subscribers_[srvc];
    if (s.in_use != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: a second subscription to service %u is not "
            "modeled", srvc);
    }
    s = Subscriber{1u, handle, callback, reply.self_pid, reply.peer_pid, reply.peer_cid};
    LOG(SocModem, "msm8255 handset remote server: subscribe service %u class %u host os %u "
                  "handle %u callback %u\n", srvc, id, host_os, handle, callback);
    return kHsTrue;
}

uint32_t Msm8255HandsetRemoteServer::AnswerCommand(uint32_t proc,
                                                   const std::vector<uint32_t>& args) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireArgWords(*this, proc, args, kCmdArgWords);
    const uint32_t present = args[0], ver = args[1], id = args[2], handle = args[3];
    const uint32_t disc1 = args[4], input_present = args[5], input = args[6];
    const uint32_t disc2 = args[8], output_len = args[9], delayed = args[10];
    codec.RequireBoolean(*this, proc, delayed);
    const Subscriber& s = subscribers_[kSrvcSendCmd];
    if (present != kXdrTrue || ver != kCmdVer || disc1 != id || disc2 != id ||
        input_present != kXdrTrue || output_len != 0u || s.in_use == 0u || handle != s.handle) {
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: command present %u ver %u id %u handle %u disc %u/%u "
            "input present %u output length %u with %u command subscribers is not modeled",
            present, ver, id, handle, disc1, disc2, input_present, output_len, s.in_use);
    }
    switch (id) {
    case kCmdSetPwrKeyThold:
        if (input != 0u) {
            emu_.Get<Fatal>().Die(
                "msm8255 handset remote server: power key threshold %u is not modeled", input);
        }
        break;
    case kCmdSetPwrKeyRstThold:
        ApplyPowerKeyResetThreshold(input);
        break;
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: command id %u with input 0x%08X is not modeled",
            id, input);
    }
    LOG(SocModem, "msm8255 handset remote server: command %u input 0x%08X\n", id, input);
    return kHsTrue;
}

void Msm8255HandsetRemoteServer::ApplyPowerKeyResetThreshold(uint32_t value) {
    const uint32_t hold_ms = value & kRstHoldMsMask;
    if ((value & ~(kRstHoldMsMask | kRstPowerOffBit | kRstHwTimerBit)) != 0u ||
        (value & kRstHwTimerBit) == 0u || hold_ms == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 handset remote server: power key reset threshold word 0x%08X is not "
            "modeled", value);
    }
    emu_.Get<Msm8255SystemPower>().ConfigurePowerKeyReset(hold_ms,
                                                          (value & kRstPowerOffBit) != 0u);
}

void Msm8255HandsetRemoteServer::ReportPowerKey(bool down) {
    const Subscriber& s = subscribers_[kSrvcRcvEvnt];
    if (s.in_use == 0u) return;
    const uint32_t code = down ? kHsPwrKey : kHsRelKey;
    const uint32_t parm = down ? 0u : kHsPwrKey;
    LOG(SocModem, "msm8255 handset remote server: key event code 0x%02X parm 0x%02X\n", code, parm);
    emu_.Get<Msm8255ModemCallbacks>().Queue(Msm8255ModemCallback{
        kHandsetCbProg, kHandsetCbVers, kHandsetCbClientCid, kHandsetCbProcEvent, s.self_pid,
        Msm8255RpcEndpoint{s.peer_pid, s.peer_cid},
        {s.callback, kXdrTrue, kKeyDataVer, code, parm, 0u}, {}, 0u, 0});
}

void Msm8255HandsetRemoteServer::SaveState(StateWriter& w) {
    for (uint32_t i = 0; i < kSrvcCount; ++i) {
        const std::string p = "handset_subscriber" + std::to_string(i) + "_";
        const Subscriber& s = subscribers_[i];
        w.Write<uint32_t>((p + "in_use").c_str(), s.in_use);
        w.Write<uint32_t>((p + "handle").c_str(), s.handle);
        w.Write<uint32_t>((p + "callback").c_str(), s.callback);
        w.Write<uint32_t>((p + "self_pid").c_str(), s.self_pid);
        w.Write<uint32_t>((p + "peer_pid").c_str(), s.peer_pid);
        w.Write<uint32_t>((p + "peer_cid").c_str(), s.peer_cid);
    }
}

void Msm8255HandsetRemoteServer::RestoreState(StateReader& r) {
    for (uint32_t i = 0; i < kSrvcCount; ++i) {
        const std::string p = "handset_subscriber" + std::to_string(i) + "_";
        Subscriber& s = subscribers_[i];
        r.Read((p + "in_use").c_str(), s.in_use);
        r.Read((p + "handle").c_str(), s.handle);
        r.Read((p + "callback").c_str(), s.callback);
        r.Read((p + "self_pid").c_str(), s.self_pid);
        r.Read((p + "peer_pid").c_str(), s.peer_pid);
        r.Read((p + "peer_cid").c_str(), s.peer_cid);
    }
}

}
