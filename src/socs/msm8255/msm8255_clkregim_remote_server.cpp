#include "msm8255_clock_enables.h"
#include "msm8255_clock_rates.h"
#include "msm8255_clock_reset.h"
#include "msm8255_oncrpc_codec.h"
#include "msm8255_rpc_server.h"
#include "msm8255_rpc_server_registry.h"
#include "msm8255_rpcrouter_wire.h"
#include "msm8255_value_set.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kClkProg = 0x3000000Fu;
constexpr uint32_t kClkVers = 0x00030001u;
constexpr uint32_t kClkCid  = 3u;

constexpr uint32_t kProcClockReset     = 2u;
constexpr uint32_t kProcResetAssert    = 3u;
constexpr uint32_t kProcResetDeassert  = 4u;
constexpr uint32_t kProcClockEnable    = 5u;
constexpr uint32_t kProcClockDisable   = 6u;
constexpr uint32_t kProcClockIsEnabled = 8u;
constexpr uint32_t kProcSelSdc1Clk     = 16u;
constexpr uint32_t kProcSelSdc2Clk     = 17u;
constexpr uint32_t kProcSelSdc3Clk     = 18u;
constexpr uint32_t kProcSelSdc4Clk     = 19u;
constexpr uint32_t kProcConfigMdhClk   = 24u;
constexpr uint32_t kProcGetClkFreqKhz  = 28u;
constexpr uint32_t kProcSelVfeClk      = 30u;
constexpr uint32_t kProcSelCameraClk   = 31u;
constexpr uint32_t kProcRailDisable    = 33u;
constexpr uint32_t kProcRailEnable     = 34u;
constexpr uint32_t kProcSelRailControl = 40u;
constexpr uint32_t kProcSelClkFreqHz   = 42u;

constexpr uint32_t kRailControlPayloadBytes = kPacmarkBytes + kCallArgsOff + 8u;

constexpr uint32_t kSdcClkSelValues[] = {0u, 6u, 7u, 9u};

constexpr uint32_t kSdcClkSelCount =
    sizeof(kSdcClkSelValues) / sizeof(kSdcClkSelValues[0]);

constexpr uint32_t kVfeClkSrcValues[] = {1u};

constexpr uint32_t kVfeClkSrcCount =
    sizeof(kVfeClkSrcValues) / sizeof(kVfeClkSrcValues[0]);

constexpr uint32_t kClockPayloadBytes = kPacmarkBytes + kCallArgsOff + 4u;
constexpr uint32_t kClockResultWords  = 0u;

constexpr uint32_t kArg0Off = kCallArgsOff + 0u;
constexpr uint32_t kArg1Off = kCallArgsOff + 4u;
constexpr uint32_t kArg2Off = kCallArgsOff + 8u;

constexpr uint32_t kThreeArgPayloadBytes =
    kPacmarkBytes + kCallArgsOff + 12u;

constexpr uint32_t kResultWords = 1u;

constexpr uint32_t kRailClock[] = {0u,  187u, 110u, 61u, 39u,
                                   52u, 112u, 92u,  29u, 189u};

constexpr uint32_t kRailCount = sizeof(kRailClock) / sizeof(kRailClock[0]);

constexpr uint32_t kNoClock = 0u;

constexpr uint32_t kCameraClockMatchMode = 2u;

class Msm8255ClkregimRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
    }

    uint32_t ServerProg() const override { return kClkProg; }
    uint32_t ServerVers() const override { return kClkVers; }
    uint32_t ServerCid() const override { return kClkCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        emu_.Get<Msm8255ClockRates>().SaveState(w);
        emu_.Get<Msm8255ClockEnables>().SaveState(w);
        emu_.Get<Msm8255ClockReset>().SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        emu_.Get<Msm8255ClockRates>().RestoreState(r);
        emu_.Get<Msm8255ClockEnables>().RestoreState(r);
        emu_.Get<Msm8255ClockReset>().RestoreState(r);
    }

private:
    uint32_t CheckedClock(uint32_t clock);
    void     CheckedSelection(const char* what, const uint32_t* values, uint32_t count, uint32_t sel);
    uint32_t RailClock(uint32_t rail);
};

uint32_t Msm8255ClkregimRemoteServer::CheckedClock(uint32_t clock) {
    if (clock >= kMsm8255ClockIdCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 clkregim remote server: clock %u is outside the %u ids "
            "this program carries", clock, kMsm8255ClockIdCount);
    }
    return clock;
}

void Msm8255ClkregimRemoteServer::CheckedSelection(const char* what, const uint32_t* values,
                                                   uint32_t count, uint32_t sel) {
    if (Msm8255ValueSetContains(values, count, sel)) {
        return;
    }
    emu_.Get<Fatal>().Die(
        "msm8255 clkregim remote server: %s selection %u is outside the "
        "%u this program answers", what, sel, count);
}

uint32_t Msm8255ClkregimRemoteServer::RailClock(uint32_t rail) {
    if (rail >= kRailCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 clkregim remote server: rail %u is outside the %u this "
            "program carries", rail, kRailCount);
    }
    const uint32_t clock = kRailClock[rail];
    if (clock == kNoClock) {
        emu_.Get<Fatal>().Die(
            "msm8255 clkregim remote server: rail %u names no clock", rail);
    }
    return clock;
}

uint32_t Msm8255ClkregimRemoteServer::AnswerCall(
    const uint8_t* payload, uint32_t size, uint32_t out_pa, uint32_t out_cap,
    uint32_t self_pid, uint32_t peer_pid, uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kClkCid, peer_pid, peer_cid, call.xid};

    if (call.proc == kProcClockEnable || call.proc == kProcClockDisable ||
        call.proc == kProcRailEnable || call.proc == kProcRailDisable) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        const uint32_t arg = codec.CallWord(call, kArg0Off);
        const bool by_rail = call.proc == kProcRailEnable ||
                             call.proc == kProcRailDisable;
        const bool on      = call.proc == kProcClockEnable ||
                             call.proc == kProcRailEnable;
        emu_.Get<Msm8255ClockEnables>().Record(CheckedClock(by_rail ? RailClock(arg) : arg), on);
        return codec.WriteAcceptedReply(reply, nullptr, kClockResultWords);
    }

    if (call.proc == kProcSelRailControl) {
        codec.RequireCallBytes(*this, call.proc, size, kRailControlPayloadBytes);
        RailClock(codec.CallWord(call, kArg0Off));
        return codec.WriteAcceptedReply(reply, nullptr, kClockResultWords);
    }

    if (call.proc == kProcSelSdc1Clk || call.proc == kProcSelSdc2Clk ||
        call.proc == kProcSelSdc3Clk || call.proc == kProcSelSdc4Clk) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        CheckedSelection("sdc clock", kSdcClkSelValues, kSdcClkSelCount, codec.CallWord(call, kArg0Off));
        return codec.WriteAcceptedReply(reply, nullptr, kClockResultWords);
    }

    if (call.proc == kProcSelVfeClk) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        CheckedSelection("vfe clock source", kVfeClkSrcValues, kVfeClkSrcCount, codec.CallWord(call, kArg0Off));
        return codec.WriteAcceptedReply(reply, nullptr, kClockResultWords);
    }

    if (call.proc == kProcClockReset) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        emu_.Get<Msm8255ClockReset>().Pulse(CheckedClock(codec.CallWord(call, kArg0Off)));
        return codec.WriteAcceptedReply(reply, nullptr, kClockResultWords);
    }

    if (call.proc == kProcResetAssert || call.proc == kProcResetDeassert) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        const uint32_t clock = CheckedClock(codec.CallWord(call, kArg0Off));
        auto&          reset = emu_.Get<Msm8255ClockReset>();
        if (call.proc == kProcResetAssert) reset.Assert(clock);
        else                               reset.Deassert(clock);
        return codec.WriteAcceptedReply(reply, nullptr, kClockResultWords);
    }

    if (call.proc == kProcClockIsEnabled) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        const uint32_t results[kResultWords] = {
            emu_.Get<Msm8255ClockEnables>().Enabled(CheckedClock(codec.CallWord(call, kArg0Off)))
                ? kXdrTrue : kXdrFalse};
        return codec.WriteAcceptedReply(reply, results, kResultWords);
    }

    if (call.proc == kProcGetClkFreqKhz) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        const uint32_t results[kResultWords] = {
            emu_.Get<Msm8255ClockRates>().ReportClockFreqKhz(codec.CallWord(call, kArg0Off))};
        return codec.WriteAcceptedReply(reply, results, kResultWords);
    }

    if (call.proc == kProcSelCameraClk) {
        codec.RequireCallBytes(*this, call.proc, size, kClockPayloadBytes);
        const uint32_t results[kResultWords] = {emu_.Get<Msm8255ClockRates>().GrantClockFreqHz(
            kMsm8255CameraClock, codec.CallWord(call, kArg0Off), kCameraClockMatchMode)};
        return codec.WriteAcceptedReply(reply, results, kResultWords);
    }

    if (call.proc != kProcConfigMdhClk && call.proc != kProcSelClkFreqHz) {
        if (size >= kClockPayloadBytes) {
            emu_.Get<Fatal>().Die(
                "msm8255 clkregim remote server: rpc procedure %u with a "
                "%u-byte payload carrying first argument %u is not modeled",
                call.proc, size, codec.CallWord(call, kArg0Off));
        }
        emu_.Get<Fatal>().Die(
            "msm8255 clkregim remote server: rpc procedure %u with a %u-byte "
            "payload carrying no argument is not modeled", call.proc, size);
    }
    codec.RequireCallBytes(*this, call.proc, size, kThreeArgPayloadBytes);

    const uint32_t arg0 = codec.CallWord(call, kArg0Off);
    const uint32_t arg1 = codec.CallWord(call, kArg1Off);
    const uint32_t arg2 = codec.CallWord(call, kArg2Off);

    auto& rates = emu_.Get<Msm8255ClockRates>();

    const uint32_t results[kResultWords] = {
        call.proc == kProcConfigMdhClk
            ? rates.GrantMdhRateKhz(arg0, arg1, arg2)
            : rates.GrantClockFreqHz(arg0, arg1, arg2)};
    return codec.WriteAcceptedReply(reply, results, kResultWords);
}

}

REGISTER_SERVICE(Msm8255ClkregimRemoteServer);

}
