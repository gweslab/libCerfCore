#include "compaq_ipaq_h3000_micro_display.h"

#include "compaq_ipaq_h3000_family_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint8_t kBacklightAuto        = 1;
constexpr uint8_t kBacklightManual      = 2;
constexpr uint8_t kBacklightReadSensor  = 3;
constexpr uint8_t kSensorQueryArg1      = 1;
constexpr uint8_t kSensorQueryArg2      = 0;
constexpr uint8_t kControlBacklight     = 1;
constexpr uint8_t kControlContrast      = 2;
constexpr uint8_t kAmbientLightReading  = 0;

constexpr uint32_t kControlBacklightLoad = 5;

struct BacklightStep {
    uint8_t  pwm;
    uint32_t load;
};

constexpr BacklightStep kBacklightSteps[] = {
    {0, 0}, {1, 5}, {15, 12}, {45, 17}, {80, 25},
};

const BacklightStep* StepFor(uint8_t pwm) {
    for (const BacklightStep& s : kBacklightSteps) {
        if (s.pwm == pwm) return &s;
    }
    return nullptr;
}

}

REGISTER_SERVICE(CompaqIpaqH3000MicroDisplay);

bool CompaqIpaqH3000MicroDisplay::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

uint8_t CompaqIpaqH3000MicroDisplay::Backlight(const uint8_t* req, uint8_t len, uint8_t* reply) {
    if (len != 3) {
        emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroDisplay: backlight request of %u bytes", len);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    switch (req[0]) {
        case kBacklightAuto:
        case kBacklightManual:
            if (StepFor(req[2]) == nullptr) break;
            backlight_mode_  = req[0];
            backlight_on_    = req[1];
            backlight_level_ = req[2];
            return 0;
        case kBacklightReadSensor:
            if (req[1] != kSensorQueryArg1 || req[2] != kSensorQueryArg2) break;
            reply[0] = kAmbientLightReading;
            return 1;
        default:
            break;
    }
    emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroDisplay: backlight request %02X %02X %02X",
                          req[0], req[1], req[2]);
}

void CompaqIpaqH3000MicroDisplay::DisplayControl(const uint8_t* req, uint8_t len) {
    if (len != 2) {
        emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroDisplay: display control of %u bytes", len);
    }
    std::lock_guard<std::mutex> lk(mtx_);
    switch (req[0]) {
        case kControlBacklight: control_backlight_on_ = req[1]; return;
        case kControlContrast:  contrast_             = req[1]; return;
        default:
            emu_.Get<Fatal>().Die("CompaqIpaqH3000MicroDisplay: display control %02X %02X",
                                  req[0], req[1]);
    }
}

uint32_t CompaqIpaqH3000MicroDisplay::BacklightLoad() const {
    std::lock_guard<std::mutex> lk(mtx_);
    const uint32_t pwm_load     = backlight_on_ != 0u ? StepFor(backlight_level_)->load : 0u;
    const uint32_t control_load = control_backlight_on_ != 0u ? kControlBacklightLoad : 0u;
    return pwm_load + control_load;
}

void CompaqIpaqH3000MicroDisplay::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("micro_backlight_mode", backlight_mode_);
    w.Write("micro_backlight_on", backlight_on_);
    w.Write("micro_backlight_level", backlight_level_);
    w.Write("micro_control_backlight_on", control_backlight_on_);
    w.Write("micro_contrast", contrast_);
}

void CompaqIpaqH3000MicroDisplay::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("micro_backlight_mode", backlight_mode_);
    r.Read("micro_backlight_on", backlight_on_);
    r.Read("micro_backlight_level", backlight_level_);
    r.Read("micro_control_backlight_on", control_backlight_on_);
    r.Read("micro_contrast", contrast_);
}

}
