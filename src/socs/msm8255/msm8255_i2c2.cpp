#include "msm8255_i2c_controller.h"

namespace cerfcore {

namespace {

constexpr uint32_t kI2c2Base     = 0xACF00000u;
constexpr int      kI2c2VicLine  = 54;
constexpr uint32_t kI2c2Clock    = 79u;

class Msm8255I2c2
    : public cerf_msm8255_i2c_detail::Msm8255I2cController<kI2c2Base, kI2c2VicLine,
                                                           kI2c2Clock, kMsm8255I2c2Bus> {
    using Msm8255I2cController::Msm8255I2cController;
};

}

REGISTER_SERVICE(Msm8255I2c2);

}
