#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/state/hibernation.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../host/audio_activity_widget.h"
#include "../../host/paced_wave_out.h"
#include "../../socs/guest_sample_clock.h"
#include "../../socs/rated_tick_count.h"
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_dac.h"
#include "../../socs/sh77xx/sh77xx_pfc.h"
#include "../../socs/sh77xx/sh77xx_port_data.h"
#include "../../socs/sh77xx/sh77xx_port_wiring.h"
#include "hp_600_family_id.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t              kSpeakerChannel  = 1u;
constexpr uint8_t               kPortKSpeakerOff = 0x20u;
constexpr uint32_t              kRenderHz        = 44100u;
constexpr GuestCycleClock::Rate kRenderRate{kRenderHz, 1u};
constexpr uint16_t              kRenderChannels  = 1u;
constexpr uint16_t              kRenderBits      = 16u;
constexpr GuestCycleClock::Rate kFlushRate{100u, 1u};
constexpr int                   kDacMidpoint     = 0x80;
constexpr int                   kDacToPcm16      = 256;

class Hp600Sh7709Speaker : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        BoardContext& board = emu_.Get<BoardContext>();
        return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
    }

    void OnReady() override {
        clock_    = &emu_.Get<GuestCycleClock>();
        dac_      = &emu_.Get<Sh77xxDac>();
        wiring_   = &emu_.Get<Sh77xxPortWiring>();
        activity_ = &emu_.Get<AudioActivityWidget>();
        activity_->NotePresent();
        paced_.Start(emu_.Get<Fatal>(), emu_.Get<InstanceThreads>(), "Hp600Speaker", kRenderHz, kRenderChannels,
                     kRenderBits);
        samples_.SetRate(clock_->ClockRate(), kRenderRate);
        clock_->RegisterRateListener([this] { samples_.Rescale(clock_->Cycles(), clock_->ClockRate(), kRenderRate); });
        flush_.Attach([this] { Flush(); });
        dac_->AddOutputListener([this] { Update(); });
        dac_->AddRestoreListener([this] { EndStream(); });
        emu_.Get<Sh77xxPortData>().AddDriveListener([this] { Update(); });
        emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { Update(); });
    }

    void OnShutdown() override { paced_.Stop(); }

private:
    void Update() {
        Render();
        const Sh77xxDac::Output out = dac_->ChannelOutput(kSpeakerChannel);
        const bool audible = out.enabled && (wiring_->NetLevels(kSh77xxPortK) & kPortKSpeakerOff) == 0u;
        level_             = static_cast<int16_t>((static_cast<int>(out.value) - kDacMidpoint) * kDacToPcm16);
        if (audible == audible_) return;
        audible_ = audible;
        if (audible) {
            samples_.Start(clock_->Cycles());
            rendered_ = 0u;
            paced_.BeginAudioOut();
            flush_.Start(kFlushRate);
            return;
        }
        Push();
        flush_.Stop();
        paced_.FinishAudioOut();
    }

    void Render() {
        if (!audible_) return;
        const uint64_t now = samples_.TicksAt(clock_->Cycles());
        for (; rendered_ < now; ++rendered_) cerf::le::Append16(out_, static_cast<uint16_t>(level_));
    }

    void Flush() {
        Render();
        Push();
    }

    void Push() {
        if (out_.empty()) return;
        activity_->MarkTx();
        paced_.QueueOutputInHostBlocks(out_.data(), static_cast<uint32_t>(out_.size()));
        out_.clear();
    }

    void EndStream() {
        flush_.Stop();
        paced_.StopAudioOut();
        out_.clear();
        audible_ = false;
    }

    GuestCycleClock*     clock_    = nullptr;
    Sh77xxDac*           dac_      = nullptr;
    Sh77xxPortWiring*    wiring_   = nullptr;
    AudioActivityWidget* activity_ = nullptr;
    PacedWaveOut         paced_;
    RatedTickCount       samples_{emu_};
    GuestSampleClock     flush_{emu_};
    std::vector<uint8_t> out_;
    uint64_t             rendered_ = 0u;
    int16_t              level_    = 0;
    bool                 audible_  = false;
};

}

REGISTER_SERVICE(Hp600Sh7709Speaker);

}
