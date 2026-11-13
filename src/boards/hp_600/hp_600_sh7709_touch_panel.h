#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/touch/host_pen_input.h"

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

    uint16_t ReadPlate(Plate plate);

private:
    void DrivePenLine(bool pen_down);

    HostPenInput    pens_;
    Sh77xxIntc*     intc_  = nullptr;
    Sh77xxPortData* ports_ = nullptr;
};

}
