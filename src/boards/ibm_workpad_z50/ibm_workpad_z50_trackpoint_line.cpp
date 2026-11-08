#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/ibm_trackpoint/ibm_trackpoint.h"
#include "../../peripherals/relative_mouse/queued_relative_mouse_input.h"
#include "../../socs/guest_sample_clock.h"
#include "../../socs/rated_tick_count.h"
#include "../../socs/vr41xx/vr41xx_dsiu.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "ibm_workpad_z50_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr int                   kRtsGpio = 46;
constexpr int                   kDtrGpio = 47;
constexpr GuestCycleClock::Rate kLineTickRate{IbmTrackpoint::kLineTickHz, 1u};

class IbmWorkpadZ50TrackpointLine : public QueuedRelativeMouseInput, private IbmTrackpointHost {
public:
    using QueuedRelativeMouseInput::QueuedRelativeMouseInput;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    std::string SourceName() const override { return "TrackPoint"; }

private:
    void OnMouseReady() override {
        giu_   = &emu_.Get<Vr41xxGiu>();
        dsiu_  = &emu_.Get<Vr41xxDsiu>();
        clock_ = &emu_.Get<GuestCycleClock>();
        if (!ticks_.SetRate(clock_->ClockRate(), kLineTickRate)) {
            emu_.Get<Fatal>().Die("IBM TrackPoint: the line clock does not fit the core clock");
        }
        ticks_.Start(clock_->Cycles());
        event_ = clock_->Add([this] { trackpoint_.OnLineTick(); });
        clock_->RegisterRateListener([this] { RescaleLineClock(); });
        trackpoint_.PowerOn();
        samples_.Attach([this] { trackpoint_.OnSample(); });
        samples_.Start({trackpoint_.SampleRateHz(), 1u});
        dsiu_->AttachEndpoint(&trackpoint_);
        giu_->RegisterOutputDriveListener([this] {
            trackpoint_.SetLines(LineHigh(kDtrGpio), LineHigh(kRtsGpio));
        });
        auto& reset = emu_.Get<GuestCpuReset>();
        reset.RegisterResetListener([this, &reset](ResetLineKind kind) {
            if (reset.DeliveredResetReportedWarm()) {
                trackpoint_.FinishSelfTest();
                return;
            }
            if (kind == ResetLineKind::Rtc || reset.DeliveredResetWasResume()) trackpoint_.PowerOn();
        });
    }

    void ApplyHostMotion(int dx, int dy, uint32_t button_mask) override {
        trackpoint_.HostMotion(dx, dy, button_mask);
    }

    SerialLine::LineConfig HostFormat() override { return dsiu_->TransmitFormat(); }

    void Receive(uint8_t data, const SerialLine::LineConfig& format) override {
        dsiu_->ReceiveFromLine(data, format);
    }

    void Halt(const char* what, unsigned value) override {
        emu_.Get<Fatal>().Die("IBM TrackPoint: %s 0x%X", what, value);
    }

    uint64_t LineTickNow() override { return ticks_.TicksAt(clock_->Cycles()); }

    void ArmLineTick(uint64_t tick) override {
        if (tick == kNoEvent) {
            clock_->Disarm(event_);
            return;
        }
        clock_->Arm(event_, ticks_.CycleOfTick(tick));
    }

    void RescaleLineClock() {
        if (!ticks_.Rescale(clock_->Cycles(), clock_->ClockRate(), kLineTickRate)) {
            emu_.Get<Fatal>().Die("IBM TrackPoint: the core rate change overflows the line clock");
        }
        trackpoint_.RearmTransmit();
    }

    bool LineHigh(int gpio) { return giu_->OutputDrive(gpio) != Vr41xxPinDrive::kLow; }

    Vr41xxGiu*              giu_   = nullptr;
    Vr41xxDsiu*             dsiu_  = nullptr;
    GuestCycleClock*        clock_ = nullptr;
    GuestCycleClock::Event* event_ = nullptr;
    NarrowRatedTickCount    ticks_;
    GuestSampleClock        samples_{emu_};
    IbmTrackpoint           trackpoint_{*this, samples_};
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50TrackpointLine, RelativeMouseInput);

}
