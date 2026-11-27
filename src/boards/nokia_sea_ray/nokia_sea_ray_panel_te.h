#pragma once

#include <cerfcore/core/service.h>
#include "../../execution/guest_cycle_clock.h"
#include "../../socs/oscillator_ticks.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class NokiaSeaRayPanelTe : public Service {
public:
    explicit NokiaSeaRayPanelTe(CerfEmulator& emu) : Service(emu), lines_(emu, false) {}

    bool ShouldRegister() override;
    void OnReady() override;

    void PowerOn();
    void SetScanning(bool scanning);
    void SetTearLine(uint32_t line);
    void SetTearOff();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    bool Active() const { return tear_on_ && scanning_; }
    void Plan(bool restoring);

    OscillatorTicks         lines_;
    GuestCycleClock*        clock_       = nullptr;
    GuestCycleClock::Event* edge_        = nullptr;
    bool                    scanning_    = false;
    bool                    tear_on_     = false;
    uint32_t                tear_line_   = 0;
    uint64_t                scan_anchor_ = 0;
};

}
