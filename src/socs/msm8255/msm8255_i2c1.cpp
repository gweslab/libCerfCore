#include "msm8255_i2c_controller.h"

namespace cerfcore {

namespace {

constexpr uint32_t kI2c1Base     = 0xACD00000u;
constexpr int      kI2c1VicLine  = 53;
constexpr uint32_t kI2c1Clock    = 78u;

class Msm8255I2c1
    : public cerf_msm8255_i2c_detail::Msm8255I2cController<kI2c1Base, kI2c1VicLine,
                                                           kI2c1Clock, kMsm8255I2c1Bus> {
    using Msm8255I2cController::Msm8255I2cController;
};

}

REGISTER_SERVICE(Msm8255I2c1);

}
