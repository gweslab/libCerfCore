#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/touch/host_pen_input.h"
#include "../../socs/guest_sample_clock.h"

#include <cstdint>

namespace cerfcore {

class Sh77xxIntc;
class Sh77xxPortData;

class Hp600Sh7709TouchPanel : public Service {
public:
    using Service::Service;

    enum class Plate { kX, kY };

    bool ShouldRegister() override;
    void OnReady() override;

    void PenDown(int x, int y);
    void PenMove(int x, int y);
    void PenUp();
    void TapRaw(uint16_t adc_x, uint16_t adc_y);

    uint16_t ReadPlate(Plate plate);

private:
    void ApplyPen(const HostPenInput::Pen& pen);
    void EndRawTap();
    void DrivePenLine(bool pen_down);

    HostPenInput     pens_;
    GuestSampleClock raw_hold_{emu_};
    Sh77xxIntc*      intc_    = nullptr;
    Sh77xxPortData*  ports_   = nullptr;
    bool             raw_tap_ = false;
    uint16_t         raw_x_   = 0u;
    uint16_t         raw_y_   = 0u;
};

}
