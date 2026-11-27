#include "msm8255_adsp.h"
#include "msm8255_adsp_image.h"
#include "msm8255_adsp_task.h"

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kQueueCmd1 = 10u;
constexpr uint32_t kQueueCmd2 = 11u;
constexpr uint32_t kQueueCmd3 = 12u;

constexpr uint32_t kOffCmdId  = 0u;
constexpr uint32_t kOffStream = 1u;

constexpr uint32_t kCmdCfg    = 0x0004u;
constexpr uint32_t kOffCfg    = 1u;
constexpr uint32_t kCfgSleep  = 0x0000u;
constexpr uint32_t kCfgEnable = 0xFFFFu;

constexpr uint32_t kCmdCfgDecType = 0x0001u;
constexpr uint32_t kOffDecCfg     = 2u;
constexpr uint32_t kOffDecDmMode  = 3u;
constexpr uint32_t kDecCfgUpdate  = 0x8000u;
constexpr uint32_t kDecCfgEnable  = 0x4000u;
constexpr uint32_t kDecDmModeNone = 0x0000u;
constexpr std::array<uint32_t, 19> kGuestDecTypes = {
    0u, 1u, 2u, 3u, 4u, 5u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u,
};

constexpr uint32_t kCmdDecCtrl = 0x0002u;
constexpr uint32_t kOffDecCtrl = 2u;
constexpr uint32_t kCtrlUpdate = 0x8000u;
constexpr uint32_t kCtrlPause  = 0x4000u;
constexpr uint32_t kCtrlFlush  = 0x2000u;

constexpr uint32_t kCmdAvsync         = 0x0003u;
constexpr uint32_t kOffAvsyncInterval = 2u;
constexpr uint32_t kOffAvsyncSamples  = 3u;
constexpr uint32_t kOffAvsyncBytes    = 6u;
constexpr uint32_t kCmdQueryAvsync    = 0x0006u;

constexpr uint32_t kCmdRoutingMode = 0x0007u;
constexpr uint32_t kOffRouteMode   = 2u;
constexpr uint32_t kRouteModeFtrt  = 1u;
constexpr uint32_t kRouteModeRt    = 2u;

constexpr uint32_t kCmdMixerGapless = 0x0009u;

constexpr uint32_t kCmdCfgAdecParams = 0x0000u;
constexpr uint32_t kOffAdecLength    = 2u;
constexpr uint32_t kOffAdecReserved  = 3u;
constexpr uint32_t kOffAdecRate      = 4u;
constexpr uint32_t kAdecReserved     = 0x0000u;

constexpr uint32_t kCmdSoftStep        = 0x0003u;
constexpr uint32_t kOffSoftStepTarget  = 1u;
constexpr uint32_t kOffSoftStepId      = 2u;
constexpr uint32_t kOffSoftStepControl = 3u;
constexpr uint32_t kSoftStepControlMax = 3u;

constexpr uint32_t kCmdCfgObjectParams = 0x0000u;
constexpr uint32_t kOffObjTarget       = 1u;
constexpr uint32_t kOffObjCfg          = 3u;
constexpr uint32_t kObjCfgUpdate       = 0x8000u;

constexpr uint32_t kTargetCopp = 0x0000u;
constexpr uint32_t kTargetPopp = 0xFFFFu;

constexpr uint32_t kMsgStatus      = 0x0001u;
constexpr uint32_t kStatusSleep    = 0x0000u;
constexpr uint32_t kStatusInit     = 0x0001u;
constexpr uint32_t kStatusPlay     = 0x0003u;
constexpr uint32_t kReasonNone     = 0x0000u;
constexpr uint32_t kMsgAvsync      = 0x0005u;
constexpr uint32_t kMsgCfg         = 0x0007u;
constexpr uint32_t kMsgEnaEna      = 0xFFFFu;
constexpr uint32_t kMsgEnaDis      = 0x0000u;
constexpr uint32_t kMsgRoutingAck  = 0x0009u;
constexpr uint32_t kMsgFlushAck    = 0x000Au;
constexpr uint32_t kMsgSoftStepAck = 0x000Cu;

constexpr uint32_t kWordBits = 16u;
constexpr uint32_t kWordMask = 0xFFFFu;

constexpr uint32_t kDecoders    = 5u;
constexpr uint32_t kParamsWords = kMsm8255AdspMaxCommandWords;

class Msm8255AdspAudpp : public Service, public Msm8255AdspTask {
public:
    using Service::Service;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Msm8255;
    }

    void OnReady() override { emu_.Get<Msm8255Adsp>().RegisterTask(this); }

    uint32_t Module() const override { return kMsm8255AdspModuleAudPp; }

    void Reset() override { streams_.fill(Stream{}); }

    void Command(const Msm8255AdspCommand& command) override {
        const uint32_t cmd_id = command.words[kOffCmdId];
        if (command.queue == kQueueCmd1) {
            switch (cmd_id) {
            case kCmdCfg:          Cfg(command); return;
            case kCmdCfgDecType:   CfgDecType(command); return;
            case kCmdDecCtrl:      DecCtrl(command); return;
            case kCmdAvsync:       Avsync(command); return;
            case kCmdQueryAvsync:  QueryAvsync(command); return;
            case kCmdRoutingMode:  RoutingMode(command); return;
            case kCmdMixerGapless: MixerGapless(command); return;
            default: break;
            }
        } else if (command.queue == kQueueCmd2) {
            switch (cmd_id) {
            case kCmdCfgAdecParams: CfgAdecParams(command); return;
            case kCmdSoftStep:      SoftStep(command); return;
            default: break;
            }
        } else if (command.queue == kQueueCmd3 && cmd_id == kCmdCfgObjectParams) {
            CfgObjectParams(command);
            return;
        }
        emu_.Get<Fatal>().Die("msm8255 adsp audpp: command 0x%04X on queue %u is not modeled",
                              cmd_id, command.queue);
    }

    void SaveState(StateWriter& w) override {
        for (const Stream& s : streams_) {
            w.Write<uint32_t>("audpp_dec_enabled", s.enabled ? 1u : 0u);
            w.Write<uint32_t>("audpp_dec_type", s.type);
            w.Write<uint32_t>("audpp_dec_routing", s.routing);
            w.Write<uint32_t>("audpp_dec_configured", s.configured ? 1u : 0u);
            w.Write<uint32_t>("audpp_dec_params_length", s.params_length);
            w.WriteBytes("audpp_dec_params", s.params.data(), s.params.size() * sizeof(uint32_t));
            w.Write<uint32_t>("audpp_dec_paused", s.paused ? 1u : 0u);
            w.Write<uint32_t>("audpp_dec_avsync_interval", s.avsync_interval);
            w.Write<uint64_t>("audpp_dec_samples", s.samples);
            w.Write<uint64_t>("audpp_dec_bytes", s.bytes);
        }
    }

    void RestoreState(StateReader& r) override {
        for (Stream& s : streams_) {
            uint32_t enabled    = 0;
            uint32_t configured = 0;
            uint32_t paused     = 0;
            r.Read("audpp_dec_enabled", enabled);
            r.Read("audpp_dec_type", s.type);
            r.Read("audpp_dec_routing", s.routing);
            r.Read("audpp_dec_configured", configured);
            r.Read("audpp_dec_params_length", s.params_length);
            r.ReadBytes("audpp_dec_params", s.params.data(), s.params.size() * sizeof(uint32_t));
            r.Read("audpp_dec_paused", paused);
            r.Read("audpp_dec_avsync_interval", s.avsync_interval);
            r.Read("audpp_dec_samples", s.samples);
            r.Read("audpp_dec_bytes", s.bytes);
            s.enabled    = enabled != 0u;
            s.configured = configured != 0u;
            s.paused     = paused != 0u;
        }
    }

private:
    struct Stream {
        bool     enabled         = false;
        uint32_t type            = 0;
        uint32_t routing         = 0;
        bool     configured      = false;
        uint32_t params_length   = 0;
        std::array<uint32_t, kParamsWords> params{};
        bool     paused          = false;
        uint32_t avsync_interval = 0;
        uint64_t samples         = 0;
        uint64_t bytes           = 0;
    };

    void Post(uint32_t msg_id, const std::vector<uint32_t>& payload) {
        emu_.Get<Msm8255Adsp>().PostMessage(Module(), msg_id, kMsm8255AdspWidth16, payload);
    }

    Stream& EnabledStream(const Msm8255AdspCommand& command, const char* what) {
        const uint32_t stream = command.words[kOffStream];
        if (stream >= kDecoders || !streams_[stream].enabled) {
            emu_.Get<Fatal>().Die("msm8255 adsp audpp: %s for stream %u, which has no enabled decoder, is not modeled",
                                  what, stream);
        }
        return streams_[stream];
    }

    static uint64_t Counter48(const Msm8255AdspCommand& command, uint32_t off) {
        return uint64_t{command.words[off]} | (uint64_t{command.words[off + 1u]} << kWordBits) |
               (uint64_t{command.words[off + 2u]} << (2u * kWordBits));
    }

    static uint32_t CounterWord(uint64_t counter, uint32_t word) {
        return static_cast<uint32_t>(counter >> (word * kWordBits)) & kWordMask;
    }

    void Cfg(const Msm8255AdspCommand& command) {
        const uint32_t cfg = command.words[kOffCfg];
        if (cfg != kCfgSleep && cfg != kCfgEnable) {
            emu_.Get<Fatal>().Die("msm8255 adsp audpp: AUDPP_CMD_CFG 0x%04X is not modeled", cfg);
        }
        Post(kMsgCfg, {cfg == kCfgEnable ? kMsgEnaEna : kMsgEnaDis});
    }

    void CfgDecType(const Msm8255AdspCommand& command) {
        const uint32_t stream = command.words[kOffStream];
        const uint32_t cfg    = command.words[kOffDecCfg];
        const uint32_t type   = cfg & ~(kDecCfgUpdate | kDecCfgEnable);
        const bool enable     = (cfg & kDecCfgEnable) != 0u;
        const bool known_type = enable ? std::find(kGuestDecTypes.begin(), kGuestDecTypes.end(), type) !=
                                             kGuestDecTypes.end()
                                       : type == 0u;
        if (stream >= kDecoders || (cfg & kDecCfgUpdate) == 0u || !known_type ||
            command.words[kOffDecDmMode] != kDecDmModeNone) {
            emu_.Get<Fatal>().Die(
                "msm8255 adsp audpp: AUDPP_CMD_CFG_DEC_TYPE for stream %u with cfg 0x%04X dm_mode 0x%04X is not modeled",
                stream, cfg, command.words[kOffDecDmMode]);
        }
        streams_[stream]         = Stream{};
        streams_[stream].enabled = enable;
        streams_[stream].type    = enable ? type : 0u;
        if (!enable) {
            Post(kMsgStatus, {stream, kStatusSleep, kReasonNone});
            return;
        }
        Post(kMsgStatus, {stream, kStatusInit, kReasonNone});
        Post(kMsgStatus, {stream, kStatusPlay, kReasonNone});
    }

    void DecCtrl(const Msm8255AdspCommand& command) {
        Stream& s = EnabledStream(command, "AUDPP_CMD_DEC_CTRL");
        const uint32_t ctrl = command.words[kOffDecCtrl];
        if ((ctrl & kCtrlUpdate) == 0u || (ctrl & ~(kCtrlUpdate | kCtrlPause | kCtrlFlush)) != 0u) {
            emu_.Get<Fatal>().Die("msm8255 adsp audpp: AUDPP_CMD_DEC_CTRL 0x%04X for stream %u is not modeled",
                                  ctrl, command.words[kOffStream]);
        }
        s.paused = (ctrl & kCtrlPause) != 0u;
        if ((ctrl & kCtrlFlush) != 0u) Post(kMsgFlushAck, {command.words[kOffStream]});
    }

    void Avsync(const Msm8255AdspCommand& command) {
        Stream& s = EnabledStream(command, "AUDPP_CMD_AVSYNC");
        s.avsync_interval = command.words[kOffAvsyncInterval];
        s.samples         = Counter48(command, kOffAvsyncSamples);
        s.bytes           = Counter48(command, kOffAvsyncBytes);
    }

    void QueryAvsync(const Msm8255AdspCommand& command) {
        const Stream& s = EnabledStream(command, "AUDPP_CMD_QUERY_AVSYNC");
        Post(kMsgAvsync, {command.words[kOffStream],
                          CounterWord(s.samples, 2u), CounterWord(s.samples, 1u), CounterWord(s.samples, 0u),
                          CounterWord(s.bytes, 2u), CounterWord(s.bytes, 1u), CounterWord(s.bytes, 0u),
                          s.params[kOffAdecRate]});
    }

    void RoutingMode(const Msm8255AdspCommand& command) {
        Stream& s = EnabledStream(command, "AUDPP_CMD_ROUTING_MODE");
        const uint32_t mode = command.words[kOffRouteMode];
        if (mode != kRouteModeFtrt && mode != kRouteModeRt) {
            emu_.Get<Fatal>().Die("msm8255 adsp audpp: AUDPP_CMD_ROUTING_MODE %u for object %u is not modeled",
                                  mode, command.words[kOffStream]);
        }
        s.routing = mode;
        Post(kMsgRoutingAck, {command.words[kOffStream], mode});
    }

    void MixerGapless(const Msm8255AdspCommand& command) {
        const uint32_t stream = command.words[kOffStream];
        if (stream >= kDecoders) {
            emu_.Get<Fatal>().Die("msm8255 adsp audpp: the mixer gapless command for stream %u is not modeled",
                                  stream);
        }
    }

    void CfgAdecParams(const Msm8255AdspCommand& command) {
        Stream& s = EnabledStream(command, "AUDPP_CMD_CFG_ADEC_PARAMS");
        const uint32_t length = command.words[kOffAdecLength];
        if (s.configured || length > kParamsWords || command.words[kOffAdecReserved] != kAdecReserved) {
            emu_.Get<Fatal>().Die(
                "msm8255 adsp audpp: AUDPP_CMD_CFG_ADEC_PARAMS for stream %u (%u words, reserved 0x%04X) is not modeled",
                command.words[kOffStream], length, command.words[kOffAdecReserved]);
        }
        s.configured    = true;
        s.params_length = length;
        s.params.fill(0u);
        std::copy_n(command.words.begin(), length, s.params.begin());
    }

    void SoftStep(const Msm8255AdspCommand& command) {
        const uint32_t target  = command.words[kOffSoftStepTarget];
        const uint32_t id      = command.words[kOffSoftStepId];
        const uint32_t control = command.words[kOffSoftStepControl];
        const bool acked = target == kTargetPopp && control != 0u;
        if ((target != kTargetPopp && target != kTargetCopp) || control > kSoftStepControlMax ||
            (target == kTargetCopp && control != 0u) || (acked && id >= kDecoders)) {
            emu_.Get<Fatal>().Die(
                "msm8255 adsp audpp: the soft-step command for target 0x%04X id %u control %u is not modeled",
                target, id, control);
        }
        if (acked) Post(kMsgSoftStepAck, {target, id, control});
    }

    void CfgObjectParams(const Msm8255AdspCommand& command) {
        const uint32_t target = command.words[kOffObjTarget];
        if ((target != kTargetCopp && target != kTargetPopp) || command.words[kOffObjCfg] != kObjCfgUpdate) {
            emu_.Get<Fatal>().Die(
                "msm8255 adsp audpp: AUDPP_CMD_CFG_OBJECT_PARAMS for target 0x%04X with cfg 0x%04X is not modeled",
                target, command.words[kOffObjCfg]);
        }
    }

    std::array<Stream, kDecoders> streams_{};
};

}

REGISTER_SERVICE(Msm8255AdspAudpp);

}
