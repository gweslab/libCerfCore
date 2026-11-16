#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace cerfcore {

struct SiemensSimaticMobilePanelsGen2OatEntry {
    uint32_t va;
    uint32_t pa_field;
    uint32_t size;
    uint32_t flags;

    uint32_t Pa() const { return (pa_field & 0xFFFFFFF0u) << 8; }
};

struct SiemensSimaticMobilePanelsGen2OatSection {
    uint32_t va;
    uint32_t pa;
    uint32_t mb;
};

struct SiemensSimaticMobilePanelsGen2RomOat {
    uint32_t table_va = 0;
    uint32_t magic_va = 0;
    std::vector<SiemensSimaticMobilePanelsGen2OatEntry> entries;
    std::vector<SiemensSimaticMobilePanelsGen2OatSection> sections;
    bool table_outside_image = false;

    bool valid() const { return magic_va != 0; }
};

SiemensSimaticMobilePanelsGen2RomOat FindSiemensSimaticMobilePanelsGen2OatInRom(std::span<const uint8_t> flat,
                                                                                 uint32_t flat_base_va, uint32_t entry_va);

}
