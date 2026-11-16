#include "siemens_simatic_mobile_panels_gen2_touch_calibration.h"

#include <cerfcore/boot/rom_parser_service.h>

#include <cstddef>
#include <cstring>
#include <vector>

namespace cerfcore {

bool SiemensSimaticMobilePanelsGen2TouchCalibration::RomHasSuffixedNames(const ParsedRom& rom) {
    static const char marker[] = "CalibrationData7_9in";
    std::vector<uint8_t> needle;
    for (const char* p = marker; *p; ++p) {
        needle.push_back(static_cast<uint8_t>(*p));
        needle.push_back(0u);
    }
    for (size_t at = 0; at + needle.size() <= rom.flat.size(); ++at)
        if (std::memcmp(rom.flat.data() + at, needle.data(), needle.size()) == 0) return true;
    return false;
}

const SiemensSimaticMobilePanelsGen2TouchCalibrationRow*
SiemensSimaticMobilePanelsGen2TouchCalibration::RowsFor(const char* panel_size) const {
    std::size_t count = 0;
    const auto* rows = Rows(count);
    for (std::size_t i = 0; i < count; ++i)
        if (std::strcmp(panel_size, rows[i].panel_size) == 0) return &rows[i];
    return nullptr;
}

}
