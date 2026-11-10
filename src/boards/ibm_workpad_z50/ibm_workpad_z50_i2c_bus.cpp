#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>
#include "../../peripherals/i2c/i2c_bit_bang_target.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include "ibm_workpad_z50_battery_gauge.h"
#include "ibm_workpad_z50_board_eeprom.h"
#include "ibm_workpad_z50_family_id.h"

#include <optional>
#include <vector>

namespace cerfcore {

namespace {

constexpr int kSclGpio = 13;
constexpr int kSdaGpio = 14;

class IbmWorkpadZ50I2cBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    void OnReady() override {
        giu_    = &emu_.Get<Vr41xxGiu>();
        gauge_  = &emu_.Get<IbmWorkpadZ50BatteryGauge>();
        eeprom_ = &emu_.Get<IbmWorkpadZ50BoardEeprom>();
        target_.emplace([this](bool released) { DriveSda(released); },
                        std::vector<I2cSlave*>{ gauge_, eeprom_ },
                        [this](uint8_t addr, bool read) {
                            if (addr == IbmWorkpadZ50BatteryGauge::kAddress) return;
                            emu_.Get<Fatal>().Die("z50 I2C bus: no device answers address "
                                                  "0x%02X (%s)", addr, read ? "read" : "write");
                        });
        giu_->SetPinLevel(kSclGpio, true);
        giu_->SetPinLevel(kSdaGpio, true);
        giu_->RegisterOutputDriveListener([this] { OnPins(); });
        giu_->RegisterPinDeviceState([this](StateWriter& w) { SaveState(w); },
                                     [this](StateReader& r) { RestoreState(r); });
    }

private:
    void OnPins() {
        const bool scl = giu_->OutputDrive(kSclGpio) != PinDrive::kLow;
        const bool sda = giu_->OutputDrive(kSdaGpio) != PinDrive::kLow && sda_released_;
        target_->OnLines(scl, sda);
    }

    void DriveSda(bool released) {
        sda_released_ = released;
        giu_->SetPinLevel(kSdaGpio, released);
    }

    void SaveState(StateWriter& w) {
        target_->SaveState(w);
        w.Write("smbus_sda_released", static_cast<uint8_t>(sda_released_));
        gauge_->SaveState(w);
        eeprom_->SaveState(w);
    }

    void RestoreState(StateReader& r) {
        uint8_t v = 0;
        target_->RestoreState(r);
        r.Read("smbus_sda_released", v);
        sda_released_ = v != 0u;
        gauge_->RestoreState(r);
        eeprom_->RestoreState(r);
    }

    Vr41xxGiu*                      giu_    = nullptr;
    IbmWorkpadZ50BatteryGauge*      gauge_  = nullptr;
    IbmWorkpadZ50BoardEeprom*       eeprom_ = nullptr;
    std::optional<I2cBitBangTarget> target_;
    bool                            sda_released_ = true;
};

}

REGISTER_SERVICE(IbmWorkpadZ50I2cBus);

}
