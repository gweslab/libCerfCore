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

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kTimeProg = 0x30000048u;
constexpr uint32_t kTimeVers = 0x00040001u;
constexpr uint32_t kTimeCid  = 8u;

constexpr uint32_t kProcSetDefaultTime = 8u;
constexpr uint32_t kProcClientInit     = 0x12u;
constexpr uint32_t kProcRequestCb      = 0x17u;
constexpr uint32_t kProcGenoffOperation = 0x16u;

constexpr uint32_t kTimeUnitJulian    = 3u;
constexpr uint32_t kGenoffOperationSet = 0u;
constexpr uint32_t kGenoffBaseSystemTime = 5u;
constexpr uint32_t kGenoffBaseLocalTime  = 6u;
constexpr uint32_t kGenoffBaseCount      = 2u;
constexpr uint32_t kJulianWords          = 7u;

constexpr uint32_t kGenoffBaseOff      = kCallArgsOff + 0u;
constexpr uint32_t kGenoffUnitOff      = kCallArgsOff + 4u;
constexpr uint32_t kGenoffOperationOff = kCallArgsOff + 8u;
constexpr uint32_t kGenoffWord3Off     = kCallArgsOff + 12u;
constexpr uint32_t kGenoffValuePtrOff  = kCallArgsOff + 16u;
constexpr uint32_t kGenoffValueUnitOff = kCallArgsOff + 20u;
constexpr uint32_t kGenoffJulianOff    = kCallArgsOff + 24u;
constexpr uint32_t kGenoffCallBytes    = kGenoffJulianOff + kJulianWords * 4u + kPacmarkBytes;

constexpr uint32_t kClientTypeInternal = 3u;
constexpr uint32_t kProcessorApp1      = 2u;
constexpr uint32_t kErrNone            = 0u;

constexpr uint32_t kByteMax = 0xFFu;

constexpr uint32_t kIssuedClientId = 0u;

constexpr uint32_t kInitTypeOff          = kCallArgsOff + 0u;
constexpr uint32_t kInitIdPtrOff         = kCallArgsOff + 4u;
constexpr uint32_t kInitIdOff            = kCallArgsOff + 8u;
constexpr uint32_t kInitProcessorOff     = kCallArgsOff + 12u;
constexpr uint32_t kInitNoIdProcessorOff = kCallArgsOff + 8u;
constexpr uint32_t kInitCallBytes        = kPacmarkBytes + kCallArgsOff + 16u;
constexpr uint32_t kInitNoIdCallBytes    = kPacmarkBytes + kCallArgsOff + 12u;

constexpr uint32_t kCbClientIdOff = kCallArgsOff + 0u;
constexpr uint32_t kCbIdOff       = kCallArgsOff + 4u;
constexpr uint32_t kCbCallBytes   = kPacmarkBytes + kCallArgsOff + 8u;

constexpr uint32_t kNoArgCallBytes = kPacmarkBytes + kCallArgsOff;

class Msm8255TimeRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            ClearClient();
            genoff_set_.fill(0u);
            genoff_julian_.fill(0u);
        });
    }

    uint32_t ServerProg() const override { return kTimeProg; }
    uint32_t ServerVers() const override { return kTimeVers; }
    uint32_t ServerCid() const override { return kTimeCid; }
    void ReleaseClient(const Msm8255RpcEndpoint& client) override {
        if (Msm8255RpcEndpoint{client_pid_, client_cid_} == client) {
            ClearClient();
        } else if (Msm8255RpcEndpoint{callback_pid_, callback_cid_} == client) {
            ClearCallback();
        }
    }

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("time_client_registered", client_registered_);
        w.Write<uint32_t>("time_client_pid", client_pid_);
        w.Write<uint32_t>("time_client_cid", client_cid_);
        w.Write<uint32_t>("time_callback_registered", callback_registered_);
        w.Write<uint32_t>("time_callback_id", callback_id_);
        w.Write<uint32_t>("time_callback_pid", callback_pid_);
        w.Write<uint32_t>("time_callback_cid", callback_cid_);
        w.WriteBytes("time_genoff_set", genoff_set_.data(), sizeof(genoff_set_));
        w.WriteBytes("time_genoff_julian", genoff_julian_.data(), sizeof(genoff_julian_));
    }

    void RestoreState(StateReader& r) override {
        r.Read("time_client_registered", client_registered_);
        r.Read("time_client_pid", client_pid_);
        r.Read("time_client_cid", client_cid_);
        r.Read("time_callback_registered", callback_registered_);
        r.Read("time_callback_id", callback_id_);
        r.Read("time_callback_pid", callback_pid_);
        r.Read("time_callback_cid", callback_cid_);
        r.ReadBytes("time_genoff_set", genoff_set_.data(), sizeof(genoff_set_));
        r.ReadBytes("time_genoff_julian", genoff_julian_.data(), sizeof(genoff_julian_));
    }

private:
    void ClearCallback() {
        callback_registered_ = 0u;
        callback_id_         = 0u;
        callback_pid_        = 0u;
        callback_cid_        = 0u;
    }

    void ClearClient() {
        client_registered_ = 0u;
        client_pid_        = 0u;
        client_cid_        = 0u;
        ClearCallback();
    }

    uint32_t AnswerSetDefaultTime(const Msm8255OncrpcCall& call, uint32_t size,
                                  uint32_t out_pa, uint32_t out_cap,
                                  uint32_t self_pid, uint32_t peer_pid,
                                  uint32_t peer_cid);
    uint32_t AnswerClientInit(const Msm8255OncrpcCall& call, uint32_t size,
                              uint32_t out_pa, uint32_t out_cap,
                              uint32_t self_pid, uint32_t peer_pid,
                              uint32_t peer_cid);
    uint32_t AnswerRequestCb(const Msm8255OncrpcCall& call, uint32_t size,
                             uint32_t out_pa, uint32_t out_cap,
                             uint32_t self_pid, uint32_t peer_pid,
                             uint32_t peer_cid);
    uint32_t AnswerGenoffOperation(const Msm8255OncrpcCall& call, uint32_t size,
                                   uint32_t out_pa, uint32_t out_cap,
                                   uint32_t self_pid, uint32_t peer_pid,
                                   uint32_t peer_cid);

    uint32_t client_registered_   = 0u;
    uint32_t client_pid_          = 0u;
    uint32_t client_cid_          = 0u;
    uint32_t callback_registered_ = 0u;
    uint32_t callback_id_         = 0u;
    uint32_t callback_pid_        = 0u;
    uint32_t callback_cid_        = 0u;
    std::array<uint32_t, kGenoffBaseCount> genoff_set_{};
    std::array<uint32_t, kGenoffBaseCount * kJulianWords> genoff_julian_{};
};

uint32_t Msm8255TimeRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                             uint32_t out_pa, uint32_t out_cap,
                                             uint32_t self_pid,
                                             uint32_t peer_pid,
                                             uint32_t peer_cid) {
    const Msm8255OncrpcCall call =
        emu_.Get<Msm8255OncrpcCodec>().ParseCall(*this, payload, size);
    switch (call.proc) {
    case kProcSetDefaultTime:
        return AnswerSetDefaultTime(call, size, out_pa, out_cap, self_pid,
                                    peer_pid, peer_cid);
    case kProcClientInit:
        return AnswerClientInit(call, size, out_pa, out_cap, self_pid,
                                peer_pid, peer_cid);
    case kProcRequestCb:
        return AnswerRequestCb(call, size, out_pa, out_cap, self_pid, peer_pid,
                               peer_cid);
    case kProcGenoffOperation:
        return AnswerGenoffOperation(call, size, out_pa, out_cap, self_pid,
                                     peer_pid, peer_cid);
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: rpc procedure %u with a %u-byte "
            "payload is not modeled", call.proc, size);
    }
}

uint32_t Msm8255TimeRemoteServer::AnswerSetDefaultTime(
    const Msm8255OncrpcCall& call, uint32_t size, uint32_t out_pa,
    uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
    uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireCallBytes(*this, call.proc, size, kNoArgCallBytes);
    if (callback_registered_ != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: procedure %u with time callback %u "
            "registered is not modeled", call.proc, callback_id_);
    }
    return codec.WriteAcceptedReply(
        Msm8255OncrpcRoute{out_pa, out_cap, self_pid, kTimeCid, peer_pid, peer_cid, call.xid},
        nullptr, 0u);
}

uint32_t Msm8255TimeRemoteServer::AnswerClientInit(
    const Msm8255OncrpcCall& call, uint32_t size, uint32_t out_pa,
    uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
    uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    if (size < kInitNoIdCallBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: client init with a %u-byte payload is not modeled",
            size);
    }
    const uint32_t type   = codec.CallWord(call, kInitTypeOff);
    const uint32_t id_ptr = codec.CallWord(call, kInitIdPtrOff);
    codec.RequireBoolean(*this, call.proc, id_ptr);
    const bool has_id = id_ptr == kXdrTrue;
    codec.RequireCallBytes(*this, call.proc, size, has_id ? kInitCallBytes : kInitNoIdCallBytes);
    const uint32_t id        = has_id ? codec.CallWord(call, kInitIdOff) : 0u;
    const uint32_t processor =
        codec.CallWord(call, has_id ? kInitProcessorOff : kInitNoIdProcessorOff);

    if (type != kClientTypeInternal || id > kByteMax || processor != kProcessorApp1) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: client init of type %u, id pointer "
            "%u, id %u, processor %u is not modeled", type, id_ptr, id,
            processor);
    }
    if (client_registered_ != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: client init of type %u with client "
            "%u already registered is not modeled", type, kIssuedClientId);
    }

    client_registered_ = 1u;
    client_pid_        = peer_pid;
    client_cid_        = peer_cid;
    return codec.WriteOptionalReply(
        Msm8255OncrpcRoute{out_pa, out_cap, self_pid, kTimeCid, peer_pid, peer_cid, call.xid},
        kErrNone, id_ptr, kIssuedClientId);
}

uint32_t Msm8255TimeRemoteServer::AnswerRequestCb(
    const Msm8255OncrpcCall& call, uint32_t size, uint32_t out_pa,
    uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
    uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    codec.RequireCallBytes(*this, call.proc, size, kCbCallBytes);
    const uint32_t client = codec.CallWord(call, kCbClientIdOff);
    const uint32_t cb_id  = codec.CallWord(call, kCbIdOff);

    if (client_registered_ == 0u || client != kIssuedClientId) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: callback request for client %u, "
            "registered %u, is not modeled", client, client_registered_);
    }
    if (callback_registered_ != 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: callback %u requested with callback "
            "%u already registered is not modeled", cb_id, callback_id_);
    }

    callback_registered_ = 1u;
    callback_id_         = cb_id;
    callback_pid_        = peer_pid;
    callback_cid_        = peer_cid;
    const uint32_t results[1] = {kErrNone};
    return codec.WriteAcceptedReply(
        Msm8255OncrpcRoute{out_pa, out_cap, self_pid, kTimeCid, peer_pid, peer_cid, call.xid},
        results, 1u);
}

uint32_t Msm8255TimeRemoteServer::AnswerGenoffOperation(
    const Msm8255OncrpcCall& call, uint32_t size, uint32_t out_pa,
    uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
    uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    codec.RequireCallBytes(*this, call.proc, size, kGenoffCallBytes);
    const uint32_t base       = codec.CallWord(call, kGenoffBaseOff);
    const uint32_t unit       = codec.CallWord(call, kGenoffUnitOff);
    const uint32_t operation  = codec.CallWord(call, kGenoffOperationOff);
    const uint32_t word3      = codec.CallWord(call, kGenoffWord3Off);
    const uint32_t value_ptr  = codec.CallWord(call, kGenoffValuePtrOff);
    const uint32_t value_unit = codec.CallWord(call, kGenoffValueUnitOff);

    if ((base != kGenoffBaseSystemTime && base != kGenoffBaseLocalTime) ||
        unit != kTimeUnitJulian || operation != kGenoffOperationSet || word3 != 0u ||
        value_ptr != kXdrTrue || value_unit != unit) {
        emu_.Get<Fatal>().Die(
            "msm8255 time remote server: genoff operation %u on base %u, unit %u, "
            "word 3 %u, value pointer %u, value unit %u is not modeled",
            operation, base, unit, word3, value_ptr, value_unit);
    }

    const uint32_t slot = base - kGenoffBaseSystemTime;
    uint32_t results[2u + kJulianWords] = {value_ptr, value_unit};
    for (uint32_t i = 0; i < kJulianWords; ++i) {
        const uint32_t word = codec.CallWord(call, kGenoffJulianOff + i * 4u);
        genoff_julian_[slot * kJulianWords + i] = word;
        results[2u + i] = word;
    }
    genoff_set_[slot] = 1u;
    LOG(SocModem, "msm8255 time remote server: genoff base %u set to %u-%02u-%02u %02u:%02u:%02u\n",
        base, results[2], results[3], results[4], results[5], results[6], results[7]);
    return codec.WriteAcceptedReply(
        Msm8255OncrpcRoute{out_pa, out_cap, self_pid, kTimeCid, peer_pid, peer_cid, call.xid},
        results, 2u + kJulianWords);
}

}

REGISTER_SERVICE(Msm8255TimeRemoteServer);

}
