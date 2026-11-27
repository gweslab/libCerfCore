#include "msm8255_modem_callbacks.h"
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
#include "../../execution/engine_bridge.h"
#include "../../peripherals/battery_pack.h"
#include <cerfengine/host_request_channel.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kVbattProg = 0x30000089u;
constexpr uint32_t kVbattVers = 0x00050001u;
constexpr uint32_t kVbattCid  = 9u;

constexpr uint32_t kVbattCbProg      = 0x31000089u;
constexpr uint32_t kVbattCbProc      = 1u;
constexpr uint32_t kVbattCbClientCid = 11u;

constexpr uint32_t kChargerOnline   = 1u;
constexpr uint32_t kChargerCharging = 2u;

constexpr uint32_t kProcRegister   = 2u;
constexpr uint32_t kProcModify     = 4u;
constexpr uint32_t kProcDeregister = 5u;
constexpr uint32_t kProcReadMv     = 12u;
constexpr uint32_t kProcFilter     = 14u;

constexpr uint32_t kResultSuccessful = 0u;

constexpr uint32_t kDirVoltageUp      = 0u;
constexpr uint32_t kDirVoltageDown    = 1u;
constexpr uint32_t kDirAboveThisLevel = 2u;
constexpr uint32_t kDirBelowThisLevel = 3u;
constexpr uint32_t kDirVoltageLevel   = 4u;
constexpr uint32_t kDirAllActivity    = 5u;
constexpr uint32_t kDirChargerEvents  = 6u;

constexpr uint32_t kChargerShift = 16u;

constexpr uint32_t kByteMax    = 0xFFu;
constexpr uint32_t kHalfMax    = 0xFFFFu;

constexpr uint32_t kClientSlots = 8u;

constexpr uint32_t kWord = 4u;

constexpr uint32_t kReadMvBytes        = kPacmarkBytes + kCallArgsOff;
constexpr uint32_t kRegisterBareBytes  = kPacmarkBytes + kCallArgsOff + 5u * kWord;
constexpr uint32_t kRegisterErrorBytes = kPacmarkBytes + kCallArgsOff + 6u * kWord;
constexpr uint32_t kModifyBytes        = kPacmarkBytes + kCallArgsOff + 5u * kWord;
constexpr uint32_t kDeregisterBytes    = kPacmarkBytes + kCallArgsOff + 1u * kWord;
constexpr uint32_t kFilterBytes        = kPacmarkBytes + kCallArgsOff + 3u * kWord;

struct VbattClient {
    uint32_t in_use         = 0u;
    uint32_t voltage_mv     = 0u;
    uint32_t direction      = 0u;
    uint32_t callback_id    = kRpcNoCallback;
    uint32_t callback_data  = 0u;
    uint32_t filter_on      = 0u;
    uint32_t filter         = 0u;
    uint32_t self_pid       = 0u;
    uint32_t peer_pid       = 0u;
    uint32_t peer_cid       = 0u;
    uint32_t state          = 0u;
};

class Msm8255VbattRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        battery_       = &emu_.Get<BatteryPack>();
        host_requests_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::HostRequestChannel>();
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { clients_.fill(VbattClient{}); });
        host_requests_->RegisterListener([this] { OnHostRequest(); });
        battery_->AddChangeListener([this] {
            battery_changed_.store(true, std::memory_order_release);
            host_requests_->Request();
        });
    }

    uint32_t ServerProg() const override { return kVbattProg; }
    uint32_t ServerVers() const override { return kVbattVers; }
    uint32_t ServerCid() const override { return kVbattCid; }

    void ReleaseClient(const Msm8255RpcEndpoint& client) override {
        for (VbattClient& c : clients_) {
            if (Msm8255RpcEndpoint{c.peer_pid, c.peer_cid} == client) c = VbattClient{};
        }
    }
    bool CallbackClientCid(uint32_t& cid) const override {
        cid = kVbattCbClientCid;
        return true;
    }

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    uint32_t AnswerRegister(const Msm8255OncrpcCall& call, uint32_t size,
                            const Msm8255OncrpcRoute& reply);
    uint32_t AnswerModify(const Msm8255OncrpcCall& call, uint32_t size,
                          const Msm8255OncrpcRoute& reply);
    uint32_t AnswerDeregister(const Msm8255OncrpcCall& call, uint32_t size,
                              const Msm8255OncrpcRoute& reply);
    uint32_t AnswerFilter(const Msm8255OncrpcCall& call, uint32_t size,
                          const Msm8255OncrpcRoute& reply);

    uint32_t Arg(const Msm8255OncrpcCall& call, uint32_t index);
    VbattClient& RequireClient(uint32_t proc, uint32_t handle);
    void RequireCondition(uint32_t proc, uint32_t voltage_mv, uint32_t direction) const;

    void     OnHostRequest();
    void     Evaluate(uint32_t index);
    uint32_t ChargerBits() const;
    uint32_t Observe(const VbattClient& client) const;
    static bool Fires(uint32_t direction, uint32_t before, uint32_t now);

    BatteryPack*                    battery_       = nullptr;
    cerfengine::HostRequestChannel* host_requests_ = nullptr;
    std::atomic<bool>               battery_changed_{false};
    std::array<VbattClient, kClientSlots> clients_{};
};

void Msm8255VbattRemoteServer::OnHostRequest() {
    if (!battery_changed_.exchange(false, std::memory_order_acq_rel)) return;
    for (uint32_t i = 0; i < kClientSlots; ++i) {
        Evaluate(i);
    }
}

uint32_t Msm8255VbattRemoteServer::ChargerBits() const {
    return (battery_->ExternalPower() ? kChargerOnline : 0u) |
           (battery_->Charging() ? kChargerCharging : 0u);
}

uint32_t Msm8255VbattRemoteServer::Observe(const VbattClient& client) const {
    const uint32_t mv = battery_->VoltageMv();
    switch (client.direction) {
    case kDirVoltageUp:
    case kDirVoltageDown:    return mv;
    case kDirAboveThisLevel: return mv > client.voltage_mv ? 1u : 0u;
    case kDirBelowThisLevel: return mv < client.voltage_mv ? 1u : 0u;
    case kDirVoltageLevel:   return mv >= client.voltage_mv ? 1u : 0u;
    case kDirAllActivity:    return (mv & kHalfMax) | (ChargerBits() << kChargerShift);
    default:                 return ChargerBits();
    }
}

bool Msm8255VbattRemoteServer::Fires(uint32_t direction, uint32_t before, uint32_t now) {
    switch (direction) {
    case kDirVoltageUp:      return now > before;
    case kDirVoltageDown:    return now < before;
    case kDirAboveThisLevel:
    case kDirBelowThisLevel: return now != 0u && before == 0u;
    default:                 return now != before;
    }
}

void Msm8255VbattRemoteServer::Evaluate(uint32_t index) {
    VbattClient& c = clients_[index];
    if (c.in_use == 0u || c.callback_id == kRpcNoCallback) return;
    const uint32_t now  = Observe(c);
    const bool     fire = Fires(c.direction, c.state, now);
    c.state = now;
    if (!fire) return;
    LOG(SocModem, "msm8255 vbatt remote server: callback %u, data 0x%08X, direction %u, state %u\n",
        c.callback_id, c.callback_data, c.direction, c.state);
    emu_.Get<Msm8255ModemCallbacks>().Queue(Msm8255ModemCallback{
        kVbattCbProg, kVbattVers, kVbattCbClientCid, kVbattCbProc, c.self_pid,
        Msm8255RpcEndpoint{c.peer_pid, c.peer_cid}, {c.callback_id, c.callback_data}, {},
        index + 1u, 0, index + 1u});
}

uint32_t Msm8255VbattRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                              uint32_t out_pa, uint32_t out_cap,
                                              uint32_t self_pid,
                                              uint32_t peer_pid,
                                              uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kVbattCid, peer_pid, peer_cid, call.xid};
    switch (call.proc) {
    case kProcRegister:
        return AnswerRegister(call, size, reply);
    case kProcModify:
        return AnswerModify(call, size, reply);
    case kProcDeregister:
        return AnswerDeregister(call, size, reply);
    case kProcReadMv: {
        codec.RequireCallBytes(*this, call.proc, size, kReadMvBytes);
        const uint32_t results[1] = {battery_->VoltageMv()};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }
    case kProcFilter:
        return AnswerFilter(call, size, reply);
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: rpc procedure %u with a %u-byte "
            "payload is not modeled", call.proc, size);
    }
}

uint32_t Msm8255VbattRemoteServer::Arg(const Msm8255OncrpcCall& call, uint32_t index) {
    return emu_.Get<Msm8255OncrpcCodec>().CallWord(call, kCallArgsOff + index * kWord);
}

VbattClient& Msm8255VbattRemoteServer::RequireClient(uint32_t proc,
                                                     uint32_t handle) {
    if (handle >= kClientSlots || clients_[handle].in_use == 0u) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: procedure %u names handle %u, which "
            "no registered client holds", proc, handle);
    }
    return clients_[handle];
}

void Msm8255VbattRemoteServer::RequireCondition(uint32_t proc,
                                                uint32_t voltage_mv,
                                                uint32_t direction) const {
    if (voltage_mv > kHalfMax || direction > kDirChargerEvents) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: procedure %u with voltage word %u and "
            "direction %u is not modeled", proc, voltage_mv, direction);
    }
}

uint32_t Msm8255VbattRemoteServer::AnswerRegister(const Msm8255OncrpcCall& call,
                                                  uint32_t size,
                                                  const Msm8255OncrpcRoute& reply) {
    if (size != kRegisterBareBytes && size != kRegisterErrorBytes) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: procedure %u with a %u-byte payload is "
            "not modeled", call.proc, size);
    }
    const uint32_t voltage_mv    = Arg(call, 0u);
    const uint32_t direction     = Arg(call, 1u);
    const uint32_t callback_id   = Arg(call, 2u);
    const uint32_t callback_data = Arg(call, 3u);
    const uint32_t more          = Arg(call, 4u);
    RequireCondition(call.proc, voltage_mv, direction);

    emu_.Get<Msm8255OncrpcCodec>().RequireBoolean(*this, call.proc, more);
    const uint32_t want = more == kXdrTrue ? kRegisterErrorBytes : kRegisterBareBytes;
    if (size != want || (more == kXdrTrue && Arg(call, 5u) != kResultSuccessful)) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: register with more-data word %u in a "
            "%u-byte payload is not modeled", more, size);
    }

    uint32_t handle = 0u;
    const auto& callbacks = emu_.Get<Msm8255ModemCallbacks>();
    while (handle < kClientSlots && (clients_[handle].in_use != 0u ||
                                     callbacks.Pending(kVbattCbProg, handle + 1u))) {
        ++handle;
    }
    if (handle == kClientSlots) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: register of voltage %u direction %u "
            "with all %u client slots in use is not modeled", voltage_mv,
            direction, kClientSlots);
    }

    VbattClient& client  = clients_[handle];
    client               = VbattClient{};
    client.in_use        = 1u;
    client.voltage_mv    = voltage_mv;
    client.direction     = direction;
    client.callback_id   = callback_id;
    client.callback_data = callback_data;
    client.self_pid      = reply.self_pid;
    client.peer_pid      = reply.peer_pid;
    client.peer_cid      = reply.peer_cid;
    client.state         = Observe(client);
    const uint32_t results[1] = {handle};
    return emu_.Get<Msm8255OncrpcCodec>().WriteAcceptedReply(reply, results, 1u);
}

uint32_t Msm8255VbattRemoteServer::AnswerModify(const Msm8255OncrpcCall& call,
                                                uint32_t size,
                                                const Msm8255OncrpcRoute& reply) {
    emu_.Get<Msm8255OncrpcCodec>().RequireCallBytes(*this, call.proc, size,
                                                    kModifyBytes);
    const uint32_t handle        = Arg(call, 0u);
    const uint32_t voltage_mv    = Arg(call, 1u);
    const uint32_t direction     = Arg(call, 2u);
    const uint32_t callback_id   = Arg(call, 3u);
    const uint32_t callback_data = Arg(call, 4u);
    if (handle > kByteMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: modify carries handle word %u", handle);
    }
    VbattClient& client = RequireClient(call.proc, handle);
    RequireCondition(call.proc, voltage_mv, direction);

    client.voltage_mv    = voltage_mv;
    client.direction     = direction;
    client.callback_id   = callback_id;
    client.callback_data = callback_data;
    client.self_pid      = reply.self_pid;
    client.peer_pid      = reply.peer_pid;
    client.peer_cid      = reply.peer_cid;
    client.state         = Observe(client);
    emu_.Get<Msm8255ModemCallbacks>().CancelUnsentOf(kVbattCbProg, handle + 1u);
    const uint32_t results[1] = {kResultSuccessful};
    return emu_.Get<Msm8255OncrpcCodec>().WriteAcceptedReply(reply, results, 1u);
}

uint32_t Msm8255VbattRemoteServer::AnswerDeregister(const Msm8255OncrpcCall& call,
                                                    uint32_t size,
                                                    const Msm8255OncrpcRoute& reply) {
    emu_.Get<Msm8255OncrpcCodec>().RequireCallBytes(*this, call.proc, size,
                                                    kDeregisterBytes);
    const uint32_t handle = Arg(call, 0u);
    if (handle > kByteMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: deregister carries handle word %u",
            handle);
    }
    RequireClient(call.proc, handle) = VbattClient{};
    emu_.Get<Msm8255ModemCallbacks>().CancelUnsentOf(kVbattCbProg, handle + 1u);
    const uint32_t results[1] = {kResultSuccessful};
    return emu_.Get<Msm8255OncrpcCodec>().WriteAcceptedReply(reply, results, 1u);
}

uint32_t Msm8255VbattRemoteServer::AnswerFilter(const Msm8255OncrpcCall& call,
                                                uint32_t size,
                                                const Msm8255OncrpcRoute& reply) {
    emu_.Get<Msm8255OncrpcCodec>().RequireCallBytes(*this, call.proc, size,
                                                    kFilterBytes);
    const uint32_t handle = Arg(call, 0u);
    const uint32_t enable = Arg(call, 1u);
    const uint32_t filter = Arg(call, 2u);
    emu_.Get<Msm8255OncrpcCodec>().RequireBoolean(*this, call.proc, enable);
    if (handle > kByteMax || filter > kHalfMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 vbatt remote server: filter of handle word %u with enable "
            "word %u and filter word %u is not modeled", handle, enable, filter);
    }
    VbattClient& client = RequireClient(call.proc, handle);
    client.filter_on = enable;
    client.filter    = filter;
    const uint32_t results[1] = {kResultSuccessful};
    return emu_.Get<Msm8255OncrpcCodec>().WriteAcceptedReply(reply, results, 1u);
}

void Msm8255VbattRemoteServer::SaveState(StateWriter& w) {
    for (uint32_t i = 0; i < kClientSlots; ++i) {
        const std::string p = "vbatt_client" + std::to_string(i) + "_";
        const VbattClient& c = clients_[i];
        w.Write<uint32_t>((p + "in_use").c_str(), c.in_use);
        w.Write<uint32_t>((p + "voltage_mv").c_str(), c.voltage_mv);
        w.Write<uint32_t>((p + "direction").c_str(), c.direction);
        w.Write<uint32_t>((p + "callback_id").c_str(), c.callback_id);
        w.Write<uint32_t>((p + "callback_data").c_str(), c.callback_data);
        w.Write<uint32_t>((p + "filter_on").c_str(), c.filter_on);
        w.Write<uint32_t>((p + "filter").c_str(), c.filter);
        w.Write<uint32_t>((p + "self_pid").c_str(), c.self_pid);
        w.Write<uint32_t>((p + "peer_pid").c_str(), c.peer_pid);
        w.Write<uint32_t>((p + "peer_cid").c_str(), c.peer_cid);
        w.Write<uint32_t>((p + "state").c_str(), c.state);
    }
}

void Msm8255VbattRemoteServer::RestoreState(StateReader& r) {
    for (uint32_t i = 0; i < kClientSlots; ++i) {
        const std::string p = "vbatt_client" + std::to_string(i) + "_";
        VbattClient& c = clients_[i];
        r.Read((p + "in_use").c_str(), c.in_use);
        r.Read((p + "voltage_mv").c_str(), c.voltage_mv);
        r.Read((p + "direction").c_str(), c.direction);
        r.Read((p + "callback_id").c_str(), c.callback_id);
        r.Read((p + "callback_data").c_str(), c.callback_data);
        r.Read((p + "filter_on").c_str(), c.filter_on);
        r.Read((p + "filter").c_str(), c.filter);
        r.Read((p + "self_pid").c_str(), c.self_pid);
        r.Read((p + "peer_pid").c_str(), c.peer_pid);
        r.Read((p + "peer_cid").c_str(), c.peer_cid);
        r.Read((p + "state").c_str(), c.state);
    }
}

}

REGISTER_SERVICE(Msm8255VbattRemoteServer);

}
