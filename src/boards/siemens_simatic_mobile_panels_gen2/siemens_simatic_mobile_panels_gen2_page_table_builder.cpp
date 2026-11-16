#include "../page_table_builder.h"
#include <cerfcore/core/fatal.h>

#include "siemens_simatic_mobile_panels_gen2_oat.h"
#include "siemens_simatic_mobile_panels_gen2_oat_from_rom.h"

#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cstdint>
#include <vector>
#include "siemens_simatic_mobile_panels_gen2_board_profile.h"
#include "siemens_simatic_mobile_panels_gen2_family_id.h"
#include "../../socs/imx6/imx6_variant.h"

namespace cerfcore {

namespace {

constexpr uint32_t kOcramPa = 0x00900000u;
constexpr uint32_t kOcramSizeDualQuad = 0x00040000u;
constexpr uint32_t kOcramSizeSoloDualLite = 0x00020000u;
constexpr uint32_t kDdrSpanSize = 2u * kDdrChipSelectSize;

class SiemensSimaticMobilePanelsGen2PageTableBuilder : public PageTableBuilder {
public:
    using PageTableBuilder::PageTableBuilder;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimaticMobilePanelsGen2;
    }

    uint32_t InitStackTopPa() const override { return kInitStackTopPa; }
    uint32_t VaToPa(uint32_t va) const override;
    std::vector<DramRegion> CachedDramRegions() const override;
    std::vector<BackedRegion> BackedMemoryRegions() const override;
    uint32_t DramChipSelectBytes() const override { return kDdrChipSelectSize; }
    std::vector<DramRegion> MappedVaSpans() const override;
private:
    const std::vector<SiemensSimaticMobilePanelsGen2OatEntry>& RomSpans() const;

    static constexpr DramRegion kDram{kDramVa, kDramPa, kDramSize};

    mutable std::vector<SiemensSimaticMobilePanelsGen2OatEntry> spans_;
    mutable bool spans_read_ = false;
    mutable uint32_t spans_generation_ = 0;
};

const std::vector<SiemensSimaticMobilePanelsGen2OatEntry>& SiemensSimaticMobilePanelsGen2PageTableBuilder::RomSpans() const {
    const auto& parser = emu_.Get<RomParserService>();
    if (spans_read_ && spans_generation_ == parser.Generation()) return spans_;
    if (!parser.Primary().is_fwf)
        emu_.Get<Fatal>().UserError("Mobile Panels 2nd Gen: rom.primary %s is not a Siemens FWF; the boot loader of this board "
                                    "boots only the /flashboot image",
                                    parser.Primary().filename.c_str());
    const SiemensSimaticMobilePanelsGen2RomOat oat =
        FindSiemensSimaticMobilePanelsGen2OatInRom(parser.Primary().flat, parser.Primary().flat_base_va, parser.EntryVa());
    if (!oat.valid())
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen: no ADR.W R0 to an OEMAddressTable header after the kernel entry "
                              "0x%08X",
                              parser.EntryVa());
    if (oat.table_outside_image)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen: OEMAddressTable at VA 0x%08X points its device table at VA 0x%08X, "
                              "outside the ROM image",
                              oat.magic_va, oat.table_va);
    if (oat.sections.empty() || oat.sections[0].va != kDramVa || oat.sections[0].pa != kDramPa ||
        oat.sections[0].mb * 0x100000u != kDramSize)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen: the ROM OEMAddressTable DRAM section differs from the board map "
                              "(VA 0x%08X PA 0x%08X)",
                              oat.sections.empty() ? 0u : oat.sections[0].va, oat.sections.empty() ? 0u : oat.sections[0].pa);
    spans_ = oat.entries;
    spans_read_ = true;
    spans_generation_ = parser.Generation();
    return spans_;
}

uint32_t SiemensSimaticMobilePanelsGen2PageTableBuilder::VaToPa(uint32_t va) const {
    if (va >= kDram.va_base && va < kDram.va_base + kDram.size) return kDram.pa_base + (va - kDram.va_base);
    for (const auto& e : RomSpans()) {
        if (va >= e.va && va < e.va + e.size) return e.Pa() + (va - e.va);
    }
    emu_.Get<Fatal>().Die("SiemensSimaticMobilePanelsGen2PageTableBuilder::VaToPa: VA 0x%08X is outside the cached DDR "
                          "window and every span of the OAL OEMAddressTable the ROM declares",
                          va);
}

std::vector<DramRegion> SiemensSimaticMobilePanelsGen2PageTableBuilder::CachedDramRegions() const {
    return {kDram};
}

std::vector<BackedRegion> SiemensSimaticMobilePanelsGen2PageTableBuilder::BackedMemoryRegions() const {
    std::vector<BackedRegion> regions;

    const SiemensSimaticMobilePanelsGen2OatEntry* low_ddr = nullptr;
    for (const auto& e : RomSpans()) {
        if (e.Pa() == kDdrPa) {
            low_ddr = &e;
            break;
        }
    }
    if (!low_ddr || uint64_t(low_ddr->Pa()) + low_ddr->size > kDram.pa_base)
        emu_.Get<Fatal>().Die("Mobile Panels 2nd Gen: the ROM OEMAddressTable has no non-overlapping low DDR region");
    regions.push_back({kDdrPa, kDdrPa, kDdrSpanSize, MemoryKind::kRam});

    const auto back_on_chip = [&](uint32_t pa, uint32_t size) {
        for (const auto& e : RomSpans()) {
            if (pa < e.Pa() || pa - e.Pa() >= e.size) continue;
            regions.push_back({e.va + (pa - e.Pa()), pa, size, MemoryKind::kRam});
            return;
        }
    };
    const bool sdl = emu_.Get<Imx6Variant>().Family() == Imx6Family::SoloDualLite;
    back_on_chip(kOcramPa, sdl ? kOcramSizeSoloDualLite : kOcramSizeDualQuad);

    return regions;
}

std::vector<DramRegion> SiemensSimaticMobilePanelsGen2PageTableBuilder::MappedVaSpans() const {
    std::vector<DramRegion> regions;
    regions.push_back(kDram);
    for (const auto& e : RomSpans())
        regions.push_back({e.va, e.Pa(), e.size});
    return regions;
}

}

REGISTER_SERVICE_AS(SiemensSimaticMobilePanelsGen2PageTableBuilder, PageTableBuilder);

}
