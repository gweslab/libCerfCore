#include "../../socs/pr31x00/pr31x00_mfio_slave.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/i2c/i2c_bit_bang_target.h"
#include "../../socs/pr31x00/pr31x00_io.h"
#include <cerfcore/boards/board_context.h>
#include "../../core/board_config.h"
#include "../dram_card_param.h"
#include "philips_velo_1_dram_card_id_eeprom.h"
#include "philips_velo_1_family_id.h"

#include <cstdint>
#include <optional>

namespace cerfcore {

namespace {

constexpr uint32_t kSclPin = 18;
constexpr uint32_t kSdaPin = 20;
constexpr uint32_t kScl    = 1u << kSclPin;
constexpr uint32_t kSda    = 1u << kSdaPin;

bool Line(const Pr31x00Pins& pins, uint32_t bit) {
    return (pins.Low() & bit) == 0u;
}

class PhilipsVelo1DramCard : public Pr31x00MfioSlave {
public:
    using Pr31x00MfioSlave::Pr31x00MfioSlave;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1 &&
               emu_.Get<BoardConfig>().Enum(BoardConfigParamId::DramCard, kDramCardBytes) != 0u;
    }

    void OnReady() override {
        eeprom_ = &emu_.Get<PhilipsVelo1DramCardIdEeprom>();
        target_.emplace([this](bool released) { DriveSda(released); },
                        std::vector<I2cSlave*>{ eeprom_ }, I2cBitBangTarget::Unmatched{});
        DriveSda(true);
    }

    void OnMfioOut(const Pr31x00Pins& pins) override {
        target_->OnLines(Line(pins, kScl), Line(pins, kSda));
    }

    void SaveState(StateWriter& w) override {
        target_->SaveState(w);
        eeprom_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        target_->RestoreState(r);
        eeprom_->RestoreState(r);
    }

private:
    void DriveSda(bool level) {
        emu_.Get<Pr31x00Io>().DriveMfioInput(kSdaPin, level);
    }

    PhilipsVelo1DramCardIdEeprom*   eeprom_ = nullptr;
    std::optional<I2cBitBangTarget> target_;
};

}

REGISTER_SERVICE_AS(PhilipsVelo1DramCard, Pr31x00MfioSlave);

}
