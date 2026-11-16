#include "siemens_simatic_mobile_panels_gen2_touch_calibration.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include "siemens_simatic_mobile_panels_gen2_family_id.h"

#include <cstddef>

namespace cerfcore {

namespace {

constexpr SiemensSimaticMobilePanelsGen2TouchCalibrationRow kRows[] = {
    {"4in", {{2004, 853, 868, 3149, 3155}, {2034, 3046, 946, 977, 3054}}},
    {"7_9in", {{1996, 853, 849, 3136, 3137}, {2063, 3170, 951, 962, 3152}}},
};

class SiemensSimaticMobilePanelsGen2TouchCalibrationV13 final
    : public SiemensSimaticMobilePanelsGen2TouchCalibration {
public:
    using SiemensSimaticMobilePanelsGen2TouchCalibration::SiemensSimaticMobilePanelsGen2TouchCalibration;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        if (!board || board->GetBoardFamilyId() != BoardFamilyId::SiemensSimaticMobilePanelsGen2)
            return false;
        return !RomHasSuffixedNames(emu_.Get<RomParserService>().Primary());
    }

    const SiemensSimaticMobilePanelsGen2TouchCalibrationRow* Rows(std::size_t& count) const override {
        count = sizeof(kRows) / sizeof(kRows[0]);
        return kRows;
    }
};

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2TouchCalibrationV13,
                    SiemensSimaticMobilePanelsGen2TouchCalibration);

}
