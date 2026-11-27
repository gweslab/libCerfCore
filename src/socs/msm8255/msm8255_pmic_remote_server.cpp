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

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kPmicProg = 0x30000061u;
constexpr uint32_t kPmicVers = 0x00030001u;
constexpr uint32_t kPmicCid  = 4u;

constexpr uint32_t kProcVregSetLevel             = 3u;
constexpr uint32_t kProcSetLedIntensity          = 16u;
constexpr uint32_t kProcSecureMppConfigDigitalIn = 44u;
constexpr uint32_t kProcHsedEnable               = 66u;
constexpr uint32_t kProcLowCurrentLedExtSignal   = 71u;
constexpr uint32_t kProcLowCurrentLedCurrent     = 72u;

constexpr uint32_t kArg0Off = kCallArgsOff + 0u;
constexpr uint32_t kArg1Off = kCallArgsOff + 4u;
constexpr uint32_t kArg2Off = kCallArgsOff + 8u;

constexpr uint32_t kTwoArgPayloadBytes   = kPacmarkBytes + kCallArgsOff + 8u;
constexpr uint32_t kThreeArgPayloadBytes = kPacmarkBytes + kCallArgsOff + 12u;

constexpr uint32_t kMppCount     = 12u;
constexpr uint32_t kMppLevelLast = 7u;
constexpr uint32_t kMppDbusLast  = 3u;

constexpr uint32_t kLedTypeCount    = 2u;
constexpr uint32_t kHsedControllers = 3u;
constexpr uint32_t kHsedEnableLast  = 3u;
constexpr uint32_t kLowCurrentLeds  = 3u;
constexpr uint32_t kExtSignalLast   = 7u;
constexpr uint32_t kByteMax         = 0xFFu;
constexpr uint32_t kHalfMax         = 0xFFFFu;

constexpr uint32_t kResultWords = 1u;

/* Linux arch/arm/mach-msm pmic.c: modem_to_linux_err answers success for a
   zero reply word and maps every PM_ERR_FLAG_* bit to a failure. */
constexpr uint32_t kErrFlagNone = 0u;

/* Linux arch/arm/mach-msm include/mach/pmic.h: the count of distinct values in
   enum vreg_id, whose first is PM_VREG_MSMA_ID. */
constexpr uint32_t kVregIdCount = 40u;

constexpr uint32_t kLevelMax = 0x7FFFu;

class Msm8255PmicRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            mpp_input_.fill(0u);
            mpp_level_.fill(0u);
            mpp_dbus_.fill(0u);
            led_intensity_.fill(0u);
            hsed_enable_.fill(0u);
            low_led_signal_.fill(0u);
            low_led_current_ma_.fill(0u);
        });
    }

    uint32_t ServerProg() const override { return kPmicProg; }
    uint32_t ServerVers() const override { return kPmicVers; }
    uint32_t ServerCid() const override { return kPmicCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        w.Write("pmic_mpp_input", mpp_input_);
        w.Write("pmic_mpp_level", mpp_level_);
        w.Write("pmic_mpp_dbus", mpp_dbus_);
        w.Write("pmic_led_intensity", led_intensity_);
        w.Write("pmic_hsed_enable", hsed_enable_);
        w.Write("pmic_low_led_signal", low_led_signal_);
        w.Write("pmic_low_led_current_ma", low_led_current_ma_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("pmic_mpp_input", mpp_input_);
        r.Read("pmic_mpp_level", mpp_level_);
        r.Read("pmic_mpp_dbus", mpp_dbus_);
        r.Read("pmic_led_intensity", led_intensity_);
        r.Read("pmic_hsed_enable", hsed_enable_);
        r.Read("pmic_low_led_signal", low_led_signal_);
        r.Read("pmic_low_led_current_ma", low_led_current_ma_);
    }

private:
    void     RequireArgs(const Msm8255OncrpcCall& call, uint32_t a, uint32_t a_limit,
                         uint32_t b, uint32_t b_limit) const;
    uint32_t VregSetLevelStatus(uint32_t vreg_id, uint32_t level_mv);
    uint32_t MppConfigDigitalInputStatus(uint32_t which, uint32_t level, uint32_t dbus);

    std::array<uint32_t, kMppCount>        mpp_input_{};
    std::array<uint32_t, kMppCount>        mpp_level_{};
    std::array<uint32_t, kMppCount>        mpp_dbus_{};
    std::array<uint32_t, kLedTypeCount>    led_intensity_{};
    std::array<uint32_t, kHsedControllers> hsed_enable_{};
    std::array<uint32_t, kLowCurrentLeds>  low_led_signal_{};
    std::array<uint32_t, kLowCurrentLeds>  low_led_current_ma_{};
};

void Msm8255PmicRemoteServer::RequireArgs(const Msm8255OncrpcCall& call, uint32_t a,
                                          uint32_t a_limit, uint32_t b,
                                          uint32_t b_limit) const {
    if (a >= a_limit || b > b_limit) {
        emu_.Get<Fatal>().Die(
            "msm8255 pmic remote server: procedure %u with arguments %u and %u is "
            "not modeled", call.proc, a, b);
    }
}

uint32_t Msm8255PmicRemoteServer::MppConfigDigitalInputStatus(uint32_t which,
                                                              uint32_t level,
                                                              uint32_t dbus) {
    if (which >= kMppCount || level > kMppLevelLast || dbus > kMppDbusLast) {
        emu_.Get<Fatal>().Die(
            "msm8255 pmic remote server: secure mpp digital input of mpp %u with "
            "level %u and dbus %u is not modeled", which, level, dbus);
    }
    mpp_input_[which] = 1u;
    mpp_level_[which] = level;
    mpp_dbus_[which]  = dbus;
    return kErrFlagNone;
}

uint32_t Msm8255PmicRemoteServer::VregSetLevelStatus(uint32_t vreg_id,
                                                     uint32_t level_mv) {
    if (vreg_id >= kVregIdCount) {
        emu_.Get<Fatal>().Die(
            "msm8255 pmic remote server: vreg id %u is not one of the %u this "
            "server models", vreg_id, kVregIdCount);
    }
    if (level_mv > kLevelMax) {
        emu_.Get<Fatal>().Die(
            "msm8255 pmic remote server: vreg id %u requests level 0x%08X, "
            "which is outside the range this server models",
            vreg_id, level_mv);
    }
    return kErrFlagNone;
}

uint32_t Msm8255PmicRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                             uint32_t out_pa, uint32_t out_cap,
                                             uint32_t self_pid,
                                             uint32_t peer_pid,
                                             uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();

    const Msm8255OncrpcCall call = codec.ParseCall(*this, payload, size);

    uint32_t status = kErrFlagNone;
    switch (call.proc) {
    case kProcVregSetLevel:
        codec.RequireCallBytes(*this, call.proc, size, kTwoArgPayloadBytes);
        status = VregSetLevelStatus(codec.CallWord(call, kArg0Off),
                                    codec.CallWord(call, kArg1Off));
        break;
    case kProcSecureMppConfigDigitalIn:
        codec.RequireCallBytes(*this, call.proc, size, kThreeArgPayloadBytes);
        status = MppConfigDigitalInputStatus(codec.CallWord(call, kArg0Off),
                                             codec.CallWord(call, kArg1Off),
                                             codec.CallWord(call, kArg2Off));
        break;
    case kProcSetLedIntensity: {
        codec.RequireCallBytes(*this, call.proc, size, kTwoArgPayloadBytes);
        const uint32_t led = codec.CallWord(call, kArg0Off), level = codec.CallWord(call, kArg1Off);
        RequireArgs(call, led, kLedTypeCount, level, kByteMax);
        led_intensity_[led] = level;
        break;
    }
    case kProcHsedEnable: {
        codec.RequireCallBytes(*this, call.proc, size, kTwoArgPayloadBytes);
        const uint32_t controller = codec.CallWord(call, kArg0Off);
        const uint32_t enable     = codec.CallWord(call, kArg1Off);
        RequireArgs(call, controller, kHsedControllers, enable, kHsedEnableLast);
        hsed_enable_[controller] = enable;
        break;
    }
    case kProcLowCurrentLedExtSignal: {
        codec.RequireCallBytes(*this, call.proc, size, kTwoArgPayloadBytes);
        const uint32_t led = codec.CallWord(call, kArg0Off), signal = codec.CallWord(call, kArg1Off);
        RequireArgs(call, led, kLowCurrentLeds, signal, kExtSignalLast);
        low_led_signal_[led] = signal;
        break;
    }
    case kProcLowCurrentLedCurrent: {
        codec.RequireCallBytes(*this, call.proc, size, kTwoArgPayloadBytes);
        const uint32_t led        = codec.CallWord(call, kArg0Off);
        const uint32_t current_ma = codec.CallWord(call, kArg1Off);
        RequireArgs(call, led, kLowCurrentLeds, current_ma, kHalfMax);
        low_led_current_ma_[led] = current_ma;
        break;
    }
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 pmic remote server: rpc procedure %u with a %u-byte "
            "payload is not modeled", call.proc, size);
    }

    const uint32_t results[kResultWords] = {status};
    return codec.WriteAcceptedReply(
        Msm8255OncrpcRoute{out_pa, out_cap, self_pid, kPmicCid, peer_pid, peer_cid, call.xid},
        results, kResultWords);
}

}

REGISTER_SERVICE(Msm8255PmicRemoteServer);

}
