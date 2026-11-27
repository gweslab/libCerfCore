#include "../../peripherals/battery_pack.h"
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

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kChgProg = 0x3000001Au;
constexpr uint32_t kChgVers = 0x00040001u;
constexpr uint32_t kChgCid  = 10u;

constexpr uint32_t kProcUsbChargerConnected    = 7u;
constexpr uint32_t kProcUsbChargerDisconnected = 8u;
constexpr uint32_t kProcUsbCurrentAvailable    = 9u;
constexpr uint32_t kProcUsbCurrentUnavailable  = 10u;
constexpr uint32_t kProcGetGeneralStatus       = 12u;
constexpr uint32_t kProcBatteryClass           = 16u;
constexpr uint32_t kProcBatteryTemperature     = 17u;

constexpr uint32_t kUsbChgSdp     = 0u;
constexpr uint32_t kUsbChgCarkit  = 1u;
constexpr uint32_t kUsbChgWall    = 2u;
constexpr uint32_t kUsbChgInvalid = 3u;
constexpr uint32_t kUsbChgNone    = 0xFFFFFFFFu;

constexpr uint32_t kChargerStatusGood    = 0u;
constexpr uint32_t kChargerStatusInvalid = 3u;

constexpr uint32_t kChargerTypeNone      = 0u;
constexpr uint32_t kChargerTypeUsbPc     = 2u;
constexpr uint32_t kChargerTypeUsbWall   = 3u;
constexpr uint32_t kChargerTypeUsbCarkit = 4u;
constexpr uint32_t kChargerTypeInvalid   = 5u;

constexpr uint32_t kBatteryStatusGood = 0u;

constexpr uint32_t kBatteryLevelDead = 0u;
constexpr uint32_t kBatteryLevelWeak = 1u;
constexpr uint32_t kBatteryLevelGood = 2u;
constexpr uint32_t kBatteryLevelFull = 3u;

constexpr uint32_t kLevelWeakFromMv = 3200u;
constexpr uint32_t kLevelGoodFromMv = 3400u;
constexpr uint32_t kLevelFullFromMv = 4200u;

constexpr uint32_t kUiEventStub = 0u;

constexpr uint32_t kBatteryClassMax = 2u;
constexpr uint32_t kHalfMax         = 0xFFFFu;

constexpr uint32_t kWord = 4u;

constexpr uint32_t kNoArgBytes  = kPacmarkBytes + kCallArgsOff;
constexpr uint32_t kOneArgBytes = kPacmarkBytes + kCallArgsOff + kWord;

constexpr uint32_t kStatusWords = 11u;

uint32_t ChargerTypeFor(uint32_t usb_type) {
    switch (usb_type) {
    case kUsbChgSdp:    return kChargerTypeUsbPc;
    case kUsbChgCarkit: return kChargerTypeUsbCarkit;
    case kUsbChgWall:   return kChargerTypeUsbWall;
    case kUsbChgNone:   return kChargerTypeUsbWall;
    default:            return kChargerTypeInvalid;
    }
}

uint32_t BatteryLevelFor(uint32_t mv) {
    if (mv < kLevelWeakFromMv) return kBatteryLevelDead;
    if (mv < kLevelGoodFromMv) return kBatteryLevelWeak;
    if (mv < kLevelFullFromMv) return kBatteryLevelGood;
    return kBatteryLevelFull;
}

class Msm8255ChgRemoteServer : public Msm8255RpcServer {
public:
    using Msm8255RpcServer::Msm8255RpcServer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        battery_ = &emu_.Get<BatteryPack>();
        emu_.Get<Msm8255RpcServerRegistry>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            usb_type_       = kUsbChgNone;
            usb_current_ma_ = 0u;
            battery_class_  = 0u;
            reported_temp_  = 0u;
        });
    }

    uint32_t ServerProg() const override { return kChgProg; }
    uint32_t ServerVers() const override { return kChgVers; }
    uint32_t ServerCid() const override { return kChgCid; }
    void ReleaseClient(const Msm8255RpcEndpoint&) override {}

    uint32_t AnswerCall(const uint8_t* payload, uint32_t size, uint32_t out_pa,
                        uint32_t out_cap, uint32_t self_pid, uint32_t peer_pid,
                        uint32_t peer_cid) override;

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("chg_usb_type", usb_type_);
        w.Write<uint32_t>("chg_usb_current_ma", usb_current_ma_);
        w.Write<uint32_t>("chg_battery_class", battery_class_);
        w.Write<uint32_t>("chg_reported_temp", reported_temp_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("chg_usb_type", usb_type_);
        r.Read("chg_usb_current_ma", usb_current_ma_);
        r.Read("chg_battery_class", battery_class_);
        r.Read("chg_reported_temp", reported_temp_);
    }

private:
    uint32_t AnswerGeneralStatus(const Msm8255OncrpcCall& call, uint32_t size,
                                 const Msm8255OncrpcRoute& reply);
    uint32_t OneArg(const Msm8255OncrpcCall& call, uint32_t size);
    void     NoArg(const Msm8255OncrpcCall& call, uint32_t size);

    BatteryPack* battery_         = nullptr;
    uint32_t     usb_type_        = kUsbChgNone;
    uint32_t     usb_current_ma_  = 0u;
    uint32_t     battery_class_   = 0u;
    uint32_t     reported_temp_   = 0u;
};

uint32_t Msm8255ChgRemoteServer::AnswerCall(const uint8_t* payload, uint32_t size,
                                            uint32_t out_pa, uint32_t out_cap,
                                            uint32_t self_pid, uint32_t peer_pid,
                                            uint32_t peer_cid) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const Msm8255OncrpcCall  call = codec.ParseCall(*this, payload, size);
    const Msm8255OncrpcRoute reply{out_pa, out_cap, self_pid, kChgCid, peer_pid, peer_cid, call.xid};
    switch (call.proc) {
    case kProcUsbChargerConnected: {
        const uint32_t type = OneArg(call, size);
        if (type > kUsbChgInvalid) {
            emu_.Get<Fatal>().Die(
                "msm8255 chg remote server: usb charger connected with type %u",
                type);
        }
        usb_type_ = type;
        return codec.WriteAcceptedReply(reply, nullptr, 0u);
    }
    case kProcUsbChargerDisconnected:
        NoArg(call, size);
        usb_type_ = kUsbChgNone;
        return codec.WriteAcceptedReply(reply, nullptr, 0u);
    case kProcUsbCurrentAvailable:
        usb_current_ma_ = OneArg(call, size);
        return codec.WriteAcceptedReply(reply, nullptr, 0u);
    case kProcUsbCurrentUnavailable:
        NoArg(call, size);
        usb_current_ma_ = 0u;
        return codec.WriteAcceptedReply(reply, nullptr, 0u);
    case kProcGetGeneralStatus:
        return AnswerGeneralStatus(call, size, reply);
    case kProcBatteryClass: {
        const uint32_t cls = OneArg(call, size);
        if (cls > kBatteryClassMax) {
            emu_.Get<Fatal>().Die(
                "msm8255 chg remote server: procedure %u with battery class %u is "
                "not modeled", call.proc, cls);
        }
        battery_class_ = cls;
        return codec.WriteAcceptedReply(reply, nullptr, 0u);
    }
    case kProcBatteryTemperature: {
        const uint32_t temp = OneArg(call, size);
        if (temp > kHalfMax) {
            emu_.Get<Fatal>().Die(
                "msm8255 chg remote server: battery temperature word 0x%08X", temp);
        }
        reported_temp_ = temp;
        return codec.WriteAcceptedReply(reply, nullptr, 0u);
    }
    default:
        emu_.Get<Fatal>().Die(
            "msm8255 chg remote server: rpc procedure %u with a %u-byte payload "
            "is not modeled", call.proc, size);
    }
}

uint32_t Msm8255ChgRemoteServer::OneArg(const Msm8255OncrpcCall& call,
                                        uint32_t size) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    codec.RequireCallBytes(*this, call.proc, size, kOneArgBytes);
    return codec.CallWord(call, kCallArgsOff);
}

void Msm8255ChgRemoteServer::NoArg(const Msm8255OncrpcCall& call, uint32_t size) {
    emu_.Get<Msm8255OncrpcCodec>().RequireCallBytes(*this, call.proc, size,
                                                    kNoArgBytes);
}

uint32_t Msm8255ChgRemoteServer::AnswerGeneralStatus(const Msm8255OncrpcCall& call,
                                                     uint32_t size,
                                                     const Msm8255OncrpcRoute& reply) {
    auto& codec = emu_.Get<Msm8255OncrpcCodec>();
    const uint32_t want = OneArg(call, size);
    codec.RequireBoolean(*this, call.proc, want);
    if (want == kXdrFalse) {
        const uint32_t results[1] = {kXdrFalse};
        return codec.WriteAcceptedReply(reply, results, 1u);
    }

    const bool     charger  = battery_->ExternalPower();
    const uint32_t mv       = battery_->VoltageMv();
    const uint32_t temp     = static_cast<uint32_t>(battery_->TemperatureCelsius()) & kHalfMax;
    const uint32_t charging = charger && battery_->Charging() ? 1u : 0u;

    const uint32_t results[kStatusWords] = {
        kXdrTrue,
        charger ? kChargerStatusGood : kChargerStatusInvalid,
        charger ? ChargerTypeFor(usb_type_) : kChargerTypeNone,
        kBatteryStatusGood,
        BatteryLevelFor(mv),
        mv,
        temp,
        charger ? kXdrTrue : kXdrFalse,
        charging,
        kXdrTrue,
        kUiEventStub,
    };
    return codec.WriteAcceptedReply(reply, results, kStatusWords);
}

}

REGISTER_SERVICE(Msm8255ChgRemoteServer);

}
