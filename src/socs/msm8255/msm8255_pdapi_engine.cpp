#include "msm8255_pdapi_engine.h"

#include "msm8255_modem_callbacks.h"
#include "msm8255_pdapi_clients.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kCmdAckProc    = 0x0Du;
constexpr uint32_t kPdEventProc   = 1u;
constexpr uint32_t kPaEventProc   = 2u;
constexpr uint32_t kXtraEventProc = 5u;

constexpr uint32_t kPdEventDone           = 0x00000008u;
constexpr uint32_t kPdEventEnd            = 0x00000010u;
constexpr uint32_t kPdEventBegin          = 0x00000020u;
constexpr uint32_t kPdEventGpsBegin       = 0x00004000u;
constexpr uint32_t kPdEventGpsDone        = 0x00008000u;
constexpr uint32_t kPdEventInjectAccepted = 0x00200000u;

constexpr uint32_t kPdInfoWords      = 223u;
constexpr uint32_t kEndStatusNone    = 0u;
constexpr uint32_t kEndStatusTimeout = 12u;

constexpr uint32_t kPdCmdGetPosition    = 0u;
constexpr uint32_t kPdCmdEndSession     = 1u;
constexpr uint32_t kPdCmdInjectPosition = 3u;
constexpr uint32_t kPaCmdSet            = 0u;
constexpr uint32_t kPaCmdGet            = 1u;
constexpr uint32_t kXtraCmdSetData      = 0u;
constexpr uint32_t kXtraCmdDownload     = 1u;
constexpr uint32_t kXtraCmdValidity     = 3u;
constexpr uint32_t kXtraCmdInjectTime   = 4u;

constexpr uint32_t kXtraEventDownload    = 1u;
constexpr uint32_t kXtraEventStatus      = 2u;
constexpr uint32_t kXtraStatusUnionWords = 104u;
constexpr uint32_t kXtraFlagsWord        = 0u;
constexpr uint32_t kXtraStatusWord       = 1u;
constexpr uint32_t kXtraMaxPartWord      = 102u;
constexpr uint32_t kXtraMaxFileWord      = 103u;
constexpr uint32_t kXtraFlagStatus       = 0x001u;
constexpr uint32_t kXtraFlagMaxPart      = 0x100u;
constexpr uint32_t kXtraFlagMaxFile      = 0x200u;
constexpr uint32_t kXtraStatusSuccess    = 0u;
constexpr uint32_t kXtraStatusNotSuccess = 1u;
constexpr uint32_t kXtraMaxPartBytes     = 400u;
constexpr uint32_t kXtraMaxFileBytes     = 101600u;

constexpr uint32_t kInfoPresent = 1u;
constexpr uint32_t kPaTailWords = 2u;
constexpr int64_t  kNsPerSecond = 1000000000;
constexpr int64_t  kModemCommandDelayNs = 10000000;

struct ParamEvent {
    uint32_t type;
    uint32_t event;
};

constexpr ParamEvent kParamEvents[] = {
    {2u, 0x00000040u},  {9u, 0x00004000u},  {10u, 0x00008000u}, {11u, 0x00010000u},
    {12u, 0x00020000u}, {13u, 0x00040000u}, {17u, 0x00800000u}, {18u, 0x00400000u},
    {19u, 0x01000000u}, {20u, 0x02000000u}, {21u, 0x04000000u}, {30u, 0x00000010u},
};

constexpr uint32_t kOneWordGetTypes[] = {2u, 18u, 19u, 20u, 21u};

uint32_t ParamEventBit(uint32_t type) {
    for (const ParamEvent& e : kParamEvents) {
        if (e.type == type) return e.event;
    }
    return 0u;
}

constexpr uint32_t KindProc(Msm8255PdapiKind kind) {
    return kind == Msm8255PdapiKind::Pd   ? kPdEventProc
         : kind == Msm8255PdapiKind::Pa   ? kPaEventProc
         : kind == Msm8255PdapiKind::Xtra ? kXtraEventProc
                                          : 0u;
}

}

bool Msm8255PdapiEngine::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetSocId() == SocId::Msm8255;
}

void Msm8255PdapiEngine::OnReady() {
    timer_ = emu_.Get<GuestCycleClock>().Add([this] { ExpireSessions(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        sessions_.clear();
        params_ = {};
        emu_.Get<GuestCycleClock>().Disarm(timer_);
    });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { Rearm(); });
}

void Msm8255PdapiEngine::Acknowledge(const Msm8255PdapiCommand& cmd, uint32_t command) {
    if (cmd.callback == kRpcNoCallback) return;
    emu_.Get<Msm8255ModemCallbacks>().Queue(
        Msm8255ModemCallback{kPdapiCbProg, kPdapiCbVers, kPdapiCbClientCid, kCmdAckProc,
                             cmd.self_pid, cmd.source, {cmd.callback, cmd.data, command, 0u}, {}, 0u,
                             kModemCommandDelayNs});
}

void Msm8255PdapiEngine::Deliver(uint32_t client, Msm8255PdapiKind kind, uint32_t event,
                                 const std::vector<uint32_t>& info) {
    Msm8255PdapiClient* c = emu_.Get<Msm8255PdapiClients>().Find(client);
    if (c == nullptr || c->active == 0u) return;
    const Msm8255PdapiRegistration& reg = c->regs[static_cast<uint32_t>(kind)];
    const uint32_t bits = event & reg.mask;
    if (reg.set == 0u || reg.callback == kRpcNoCallback || bits == 0u) return;
    std::vector<uint32_t> args = {reg.callback, reg.data, bits, kInfoPresent, client};
    args.insert(args.end(), info.begin(), info.end());
    emu_.Get<Msm8255ModemCallbacks>().Queue(
        Msm8255ModemCallback{kPdapiCbProg, kPdapiCbVers, kPdapiCbClientCid, KindProc(kind),
                             reg.self_pid, reg.dest, std::move(args), {}, 0u, kModemCommandDelayNs});
}

void Msm8255PdapiEngine::DeliverPd(uint32_t client, uint32_t event, uint32_t end_status) {
    std::vector<uint32_t> info(kPdInfoWords, 0u);
    info[0] = end_status;
    Deliver(client, Msm8255PdapiKind::Pd, event, info);
}

void Msm8255PdapiEngine::DeliverPa(uint32_t client, uint32_t type,
                                   const std::vector<uint32_t>& value) {
    const uint32_t bit = ParamEventBit(type);
    if (bit == 0u) return;
    std::vector<uint32_t> info = {type, type};
    info.insert(info.end(), value.begin(), value.end());
    info.insert(info.end(), kPaTailWords, 0u);
    Deliver(client, Msm8255PdapiKind::Pa, bit, info);
}

void Msm8255PdapiEngine::StartSession(const Msm8255PdapiCommand& cmd, uint32_t client,
                                      uint32_t timeout_s) {
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [client](const Session& s) { return s.client == client; }),
                    sessions_.end());
    auto& clock = emu_.Get<GuestCycleClock>();
    sessions_.push_back(Session{
        client, clock.CycleAfterNs(static_cast<int64_t>(timeout_s) * kNsPerSecond)});
    Acknowledge(cmd, kPdCmdGetPosition);
    DeliverPd(client, kPdEventBegin | kPdEventGpsBegin, kEndStatusNone);
    LOG(SocModem, "msm8255 pdapi engine: position session for client 0x%08X, %u s timeout\n",
        client, timeout_s);
    Rearm();
}

void Msm8255PdapiEngine::EndSession(const Msm8255PdapiCommand& cmd, uint32_t client) {
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [client](const Session& s) { return s.client == client; }),
                    sessions_.end());
    Acknowledge(cmd, kPdCmdEndSession);
    Rearm();
}

void Msm8255PdapiEngine::InjectPosition(const Msm8255PdapiCommand& cmd, uint32_t client) {
    DeliverPd(client, kPdEventInjectAccepted, kEndStatusNone);
    Acknowledge(cmd, kPdCmdInjectPosition);
}

void Msm8255PdapiEngine::SetParam(const Msm8255PdapiCommand& cmd, uint32_t client,
                                  uint32_t type, std::vector<uint32_t> value) {
    if (type >= kParamTypes) {
        emu_.Get<Fatal>().Die("msm8255 pdapi engine: parameter type %u is not modeled", type);
    }
    params_[type] = std::move(value);
    Acknowledge(cmd, kPaCmdSet);
    DeliverPa(client, type, params_[type]);
}

void Msm8255PdapiEngine::GetParam(const Msm8255PdapiCommand& cmd, uint32_t client,
                                  uint32_t type) {
    if (std::find(std::begin(kOneWordGetTypes), std::end(kOneWordGetTypes), type) ==
        std::end(kOneWordGetTypes)) {
        emu_.Get<Fatal>().Die("msm8255 pdapi engine: a read of parameter type %u is not modeled",
                              type);
    }
    if (params_[type].empty()) params_[type] = {0u};
    Acknowledge(cmd, kPaCmdGet);
    DeliverPa(client, type, params_[type]);
}

void Msm8255PdapiEngine::DeliverXtraStatus(uint32_t client, uint32_t status) {
    std::vector<uint32_t> info(2u + kXtraStatusUnionWords, 0u);
    info[0] = kXtraEventStatus;
    info[1] = kXtraEventStatus;
    info[2u + kXtraFlagsWord]  = kXtraFlagStatus;
    info[2u + kXtraStatusWord] = status;
    Deliver(client, Msm8255PdapiKind::Xtra, kXtraEventStatus, info);
}

void Msm8255PdapiEngine::InjectTime(const Msm8255PdapiCommand& cmd, uint32_t client) {
    Acknowledge(cmd, kXtraCmdInjectTime);
    DeliverXtraStatus(client, kXtraStatusSuccess);
}

void Msm8255PdapiEngine::XtraDownloadRequest(const Msm8255PdapiCommand& cmd, uint32_t client) {
    Acknowledge(cmd, kXtraCmdDownload);
    std::vector<uint32_t> info(2u + kXtraStatusUnionWords, 0u);
    info[0] = kXtraEventDownload;
    info[1] = kXtraEventDownload;
    info[2u + kXtraFlagsWord]   = kXtraFlagMaxPart | kXtraFlagMaxFile;
    info[2u + kXtraMaxPartWord] = kXtraMaxPartBytes;
    info[2u + kXtraMaxFileWord] = kXtraMaxFileBytes;
    Deliver(client, Msm8255PdapiKind::Xtra, kXtraEventDownload, info);
}

void Msm8255PdapiEngine::XtraDataPart(const Msm8255PdapiCommand& cmd, uint32_t client) {
    Acknowledge(cmd, kXtraCmdSetData);
    DeliverXtraStatus(client, kXtraStatusSuccess);
}

void Msm8255PdapiEngine::XtraValidity(const Msm8255PdapiCommand& cmd, uint32_t client) {
    Acknowledge(cmd, kXtraCmdValidity);
    DeliverXtraStatus(client, kXtraStatusNotSuccess);
}

void Msm8255PdapiEngine::ExpireSessions() {
    const uint64_t now = emu_.Get<GuestCycleClock>().Cycles();
    std::vector<uint32_t> ended;
    for (const Session& s : sessions_) {
        if (s.deadline <= now) ended.push_back(s.client);
    }
    sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                   [now](const Session& s) { return s.deadline <= now; }),
                    sessions_.end());
    for (uint32_t client : ended) {
        LOG(SocModem, "msm8255 pdapi engine: position session for client 0x%08X timed out\n",
            client);
        DeliverPd(client, kPdEventEnd | kPdEventGpsDone | kPdEventDone, kEndStatusTimeout);
    }
    Rearm();
}

void Msm8255PdapiEngine::Rearm() {
    auto& clock = emu_.Get<GuestCycleClock>();
    clock.Disarm(timer_);
    if (sessions_.empty()) return;
    const auto first = std::min_element(sessions_.begin(), sessions_.end(),
                                        [](const Session& a, const Session& b) {
                                            return a.deadline < b.deadline;
                                        });
    clock.Arm(timer_, first->deadline);
}

void Msm8255PdapiEngine::SaveState(StateWriter& w) {
    w.Write("pdapi_session_count", static_cast<uint32_t>(sessions_.size()));
    for (size_t i = 0; i < sessions_.size(); ++i) {
        const std::string p = "pdapi_session" + std::to_string(i) + "_";
        w.Write((p + "client").c_str(), sessions_[i].client);
        w.Write((p + "deadline").c_str(), sessions_[i].deadline);
    }
    for (uint32_t t = 0; t < kParamTypes; ++t) {
        const std::string p = "pdapi_param" + std::to_string(t) + "_";
        w.Write((p + "words").c_str(), static_cast<uint32_t>(params_[t].size()));
        w.WriteBytes((p + "value").c_str(), params_[t].data(),
                     params_[t].size() * sizeof(uint32_t));
    }
}

void Msm8255PdapiEngine::RestoreState(StateReader& r) {
    uint32_t count = 0u;
    r.Read("pdapi_session_count", count);
    sessions_.assign(count, Session{});
    for (size_t i = 0; i < sessions_.size(); ++i) {
        const std::string p = "pdapi_session" + std::to_string(i) + "_";
        r.Read((p + "client").c_str(), sessions_[i].client);
        r.Read((p + "deadline").c_str(), sessions_[i].deadline);
    }
    for (uint32_t t = 0; t < kParamTypes; ++t) {
        const std::string p = "pdapi_param" + std::to_string(t) + "_";
        uint32_t words = 0u;
        r.Read((p + "words").c_str(), words);
        params_[t].resize(words);
        r.ReadBytes((p + "value").c_str(), params_[t].data(),
                    params_[t].size() * sizeof(uint32_t));
    }
}

REGISTER_SERVICE(Msm8255PdapiEngine);

}
