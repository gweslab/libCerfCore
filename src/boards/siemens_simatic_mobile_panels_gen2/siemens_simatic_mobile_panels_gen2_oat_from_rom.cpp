#include "siemens_simatic_mobile_panels_gen2_oat_from_rom.h"

#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace {

constexpr uint32_t kOatMagic = 0x87654321u;

}

SiemensSimaticMobilePanelsGen2RomOat FindSiemensSimaticMobilePanelsGen2OatInRom(std::span<const uint8_t> flat,
                                                                                 uint32_t flat_base_va, uint32_t entry_va) {
    if (entry_va < flat_base_va) return {};
    for (size_t at = entry_va - flat_base_va; at <= flat.size() && flat.size() - at >= 4u; at += 2u) {
        const uint32_t hw1 = cerf::le::U16(flat.data() + at);
        const uint32_t hw2 = cerf::le::U16(flat.data() + at + 2u);
        if ((hw1 & 0xFBFFu) != 0xF20Fu || (hw2 & 0x8F00u) != 0u) continue;
        const uint32_t imm = (((hw1 >> 10) & 1u) << 11) | (((hw2 >> 12) & 7u) << 8) | (hw2 & 0xFFu);
        const size_t header = ((at + 4u) & ~static_cast<size_t>(3u)) + imm;
        if (header > flat.size() || flat.size() - header < 12u || cerf::le::U32(flat.data() + header) != kOatMagic) continue;

        SiemensSimaticMobilePanelsGen2RomOat oat;
        oat.magic_va = flat_base_va + static_cast<uint32_t>(header);
        oat.table_va = cerf::le::U32(flat.data() + header + 4u);
        for (size_t off = header + 12u; off <= flat.size() && flat.size() - off >= 12u; off += 12u) {
            const SiemensSimaticMobilePanelsGen2OatSection s{cerf::le::U32(flat.data() + off),
                                                                 cerf::le::U32(flat.data() + off + 4u),
                                                                 cerf::le::U32(flat.data() + off + 8u)};
            if (s.mb == 0u) break;
            oat.sections.push_back(s);
        }
        if (oat.table_va == 0u) return oat;
        if (oat.table_va < flat_base_va || oat.table_va - flat_base_va >= flat.size()) {
            oat.table_outside_image = true;
            return oat;
        }

        for (size_t off = oat.table_va - flat_base_va; off <= flat.size() && flat.size() - off >= 16u; off += 16u) {
            const SiemensSimaticMobilePanelsGen2OatEntry e{cerf::le::U32(flat.data() + off), cerf::le::U32(flat.data() + off + 4u),
                                      cerf::le::U32(flat.data() + off + 8u), cerf::le::U32(flat.data() + off + 12u)};
            if (e.size == 0u) break;
            oat.entries.push_back(e);
        }
        return oat;
    }
    return {};
}

}
