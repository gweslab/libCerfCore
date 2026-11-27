#include "../../peripherals/akm_ak8975/akm_ak8975.h"
#include "../../peripherals/atmel_mxt224e/atmel_mxt224e.h"
#include "../../peripherals/avago_qpds_t900/avago_qpds_t900.h"
#include "../../peripherals/kionix_kxtf9/kionix_kxtf9.h"
#include "../../peripherals/ti_bq27520/ti_bq27520.h"
#include "../../peripherals/ti_bq27520/ti_bq27520_monitor.h"
#include "../../peripherals/smia_camera_sensor/smia_camera_sensor.h"
#include "../../socs/msm8255/msm8255_gpio_bus.h"
#include "../../socs/msm8255/msm8255_i2c_slaves.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "nokia_sea_ray_family_id.h"
#include "nokia_sea_ray_lens_driver.h"

#include <array>
#include <cstdint>
#include <span>

namespace cerfcore {

namespace {

constexpr uint32_t kGaugeSocIntPin    = 180u;
constexpr uint32_t kLightProximityPin = 41u;
constexpr uint32_t kCompassDrdyPin    = 7u;
constexpr uint32_t kTouchChgPin       = 42u;
constexpr uint32_t kTouchChgPinEarly  = 170u;
constexpr uint32_t kTouchChgRevision  = 3u;
constexpr uint32_t kTouchResetPin     = 74u;

class NokiaSeaRayI2cSlaves : public Msm8255I2cSlaves {
public:
    using Msm8255I2cSlaves::Msm8255I2cSlaves;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        auto& gauge   = emu_.Get<TiBq27520>();
        auto& sensor  = emu_.Get<AvagoQpdsT900>();
        auto& compass = emu_.Get<AkmAk8975>();
        auto& touch   = emu_.Get<AtmelMxt224e>();
        auto& accel   = emu_.Get<KionixKxtf9>();
        i2c1_bus_     = {&gauge, &sensor, &compass, &touch, &accel};
        qup_bus_      = {&emu_.Get<SmiaCameraSensor>(), &emu_.Get<NokiaSeaRayLensDriver>()};
        const DbBoard& board =
            *emu_.Get<BoardDatabase>().FindBoard(emu_.Get<BoardContext>().GetBoardId());
        const uint32_t chg_pin =
            *board.hw_revision < kTouchChgRevision ? kTouchChgPinEarly : kTouchChgPin;
        touch.AddChgListener([this, chg_pin](bool level) {
            emu_.Get<Msm8255GpioBus>().SetInputPin(chg_pin, level);
        });
        emu_.Get<Msm8255GpioBus>().AddOutputListener(kTouchResetPin, [this](Msm8255GpioOutput level) {
            const bool asserted = level == Msm8255GpioOutput::kLow;
            LOG(Periph, "touch controller: reset line %s by gpio %u\n", asserted ? "asserted" : "released",
                kTouchResetPin);
            emu_.Get<AtmelMxt224e>().SetResetLine(asserted);
        });
        emu_.Get<TiBq27520Monitor>().AddSocIntListener([this](bool level) {
            emu_.Get<Msm8255GpioBus>().SetInputPin(kGaugeSocIntPin, level);
        });
        sensor.AddIntListener([this](bool level) {
            emu_.Get<Msm8255GpioBus>().SetInputPin(kLightProximityPin, level);
        });
        compass.AddDrdyListener([this](bool level) {
            emu_.Get<Msm8255GpioBus>().SetInputPin(kCompassDrdyPin, level);
        });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            emu_.Get<AvagoQpdsT900>().PowerOn();
            emu_.Get<AkmAk8975>().PowerOn();
            emu_.Get<KionixKxtf9>().PowerOn();
            emu_.Get<AtmelMxt224e>().PowerOn();
            emu_.Get<SmiaCameraSensor>().PowerOn();
            emu_.Get<NokiaSeaRayLensDriver>().PowerOn();
        });
    }

    std::span<I2cSlave* const> Slaves(uint32_t bus) override {
        if (bus == kMsm8255I2c1Bus) return i2c1_bus_;
        if (bus == kMsm8255QupBus) return qup_bus_;
        return {};
    }

private:
    std::array<I2cSlave*, 5> i2c1_bus_{};
    std::array<I2cSlave*, 2> qup_bus_{};
};

}

REGISTER_SERVICE_AS(NokiaSeaRayI2cSlaves, Msm8255I2cSlaves);

}
