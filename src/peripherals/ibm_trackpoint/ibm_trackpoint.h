#pragma once

#include "../../socs/guest_sample_clock.h"
#include "../../socs/uart_endpoint.h"
#include "../ps2_mouse/ps2_mouse.h"
#include "../serial/serial_line.h"
#include "ibm_trackpoint_host.h"

#include <array>
#include <cstdint>
#include <deque>
#include <numeric>

namespace cerfcore {

class StateWriter;
class StateReader;

class IbmTrackpoint : public UartEndpoint {
    static constexpr uint32_t kPs2BaudClock   = 14745600u;
    static constexpr uint32_t kPs2BaudDivisor = 384u;
    static constexpr uint32_t kXmitGapUnitUs  = 11u;
    static constexpr uint32_t kUsPerSecond    = 1000000u;

public:
    static constexpr uint64_t kLineTickHz =
        std::lcm(kPs2BaudClock / kPs2BaudDivisor, kUsPerSecond / std::gcd(kXmitGapUnitUs, kUsPerSecond));

    IbmTrackpoint(IbmTrackpointHost& host, GuestSampleClock& samples);

    void     PowerOn();
    void     SetLines(bool dtr_high, bool rts_high);
    void     HostMotion(int dx, int dy, uint32_t buttons);
    void     OnLineTick();
    void     OnSample();
    uint32_t SampleRateHz() const;
    void     RearmTransmit();

    void OnGuestTx(uint8_t byte) override;
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    enum class Mode : uint8_t { kSerial, kPs2 };
    enum class Parse : uint8_t { kCommand, kE2Sub, kE2Arg1, kE2Arg2 };

    struct Output {
        uint8_t data  = 0;
        bool    first = false;
    };

    SerialLine::LineConfig Format() const;
    int64_t                Now() const;
    int64_t                FrameTicks() const;
    int64_t                GapTicks() const;

    void    SerialMouseReset();
    void    OnByte(uint8_t b);
    void    OnCommand(uint8_t b);
    void    OnE2Sub(uint8_t b);
    void    OnE2Arg1(uint8_t b);
    void    OnE2Arg2(uint8_t b);
    void    Reject();
    void    QueueResponse(bool reply);
    bool    LineBusy() const;
    void    TakeReport();
    void    QueueSerialReport(const Ps2MouseSample& s);
    void    DiscardUntransmitted();
    void    StartTransmit();
    void    ArmHead(int64_t ready);
    void    Disarm();
    void    LandHead();
    uint8_t ReadRam(uint8_t addr);

    IbmTrackpointHost& host_;
    GuestSampleClock&  samples_;
    Ps2Mouse           ps2_;

    Mode                     mode_       = Mode::kSerial;
    uint8_t                  baud_param_ = 0;
    std::array<uint8_t, 256> ram_{};
    std::array<uint8_t, 256> ram_known_{};
    Parse                    parse_          = Parse::kCommand;
    uint8_t                  e2_command_     = 0;
    uint8_t                  e2_arg1_        = 0;
    std::deque<Output>       tx_queue_;
    bool                     armed_          = false;
    int64_t                  start_          = 0;
    int64_t                  land_           = 0;
    bool                     has_last_land_  = false;
    int64_t                  last_land_      = 0;
    bool                     id_in_progress_ = false;
    bool                     sample_due_     = false;
    bool                     pending_ps2_    = false;
    uint8_t                  pending_param_  = 0;
    bool                     dtr_high_       = true;
    bool                     rts_high_       = true;
};

}
