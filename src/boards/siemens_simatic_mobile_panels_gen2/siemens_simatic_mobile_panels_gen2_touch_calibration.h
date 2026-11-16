#pragma once

#include <cerfcore/core/service.h>

#include <cstddef>
#include <cstdint>

namespace cerfcore {

struct ParsedRom;

struct SiemensSimaticMobilePanelsGen2TouchCalibrationPoints {
    uint16_t x[5];
    uint16_t y[5];
};

struct SiemensSimaticMobilePanelsGen2TouchCalibrationRow {
    const char* panel_size;
    SiemensSimaticMobilePanelsGen2TouchCalibrationPoints points;
};

class SiemensSimaticMobilePanelsGen2TouchCalibration : public Service {
public:
    using Service::Service;

    const SiemensSimaticMobilePanelsGen2TouchCalibrationRow* RowsFor(const char* panel_size) const;
    static bool RomHasSuffixedNames(const ParsedRom& rom);

protected:
    virtual const SiemensSimaticMobilePanelsGen2TouchCalibrationRow* Rows(std::size_t& count) const = 0;
};

}
