#include "../unmapped_window_page_table_builder.h"

#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "../../core/board_config.h"
#include "../dram_card_param.h"
#include <cerfengine/mips/mips_mmu.h>
#include "../../socs/pr31x00/pr31x00_chip_selects.h"
#include "philips_velo_1_family_id.h"
#include "philips_velo_1_internal_flash.h"
#include "philips_velo_1_rom_upgrade_card.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kDramVaBase = 0x80000000u;

/* nk.exe sub_9F40EA9C leaves ENCS1DRAM clear (MEMCONFIG0 &= 0x010FFFF1), so PA 0
   decodes DRAM BANK 0 (TMPR3911 Table 4.2.1). */
constexpr uint32_t kDramPaBase = 0x00000000u;

/* The Philips Velo 1 ships 4 MB of EDO DRAM (OEM specification). nk.exe
   sub_9F40EAE0 measures DRAM by aliasing, so the guest adopts whatever size is
   populated here. */
constexpr uint32_t kDramSize = 0x00400000u;

/* DRAM BANK 0 decodes 32 MB at PA 0 (TMPR3911 Table 4.2.1); the 4 MB population
   aliases through the window, which is how sub_9F40EAE0 sizes it. */
constexpr uint32_t kDramDecodeSpan = 0x02000000u;

/* philips_velo_1_ce1 nk.exe sub_9F42453C @0x9F42453C reports the slot-1 card base as
   0x82000000; PA 0x02000000 decodes 32 MB as DRAM BANK 1 while CS1 is not enabled
   (TMPR3911 Table 4.2.1). */
constexpr uint32_t kBank1VaBase     = 0x82000000u;
constexpr uint32_t kBank1PaBase     = 0x02000000u;
constexpr uint32_t kBank1DecodeSpan = 0x02000000u;

/* Internal Function Registers, 2 MB at PA 0x10C00000 (TMPR3911 Table 4.2.1). */
constexpr uint32_t kRegsPaBase = 0x10C00000u;
constexpr uint32_t kRegsVaBase = 0xB0C00000u;
constexpr uint32_t kRegsSize   = 0x00200000u;

class PhilipsVelo1PageTableBuilder : public UnmappedWindowPageTableBuilder {
public:
    using UnmappedWindowPageTableBuilder::UnmappedWindowPageTableBuilder;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
    }

    void OnReady() override {
        const ParsedRom& prim = PrimaryRom();

        /* Both XIPs are resident in the same CS0 flash, so the backed region spans
           the first XIP's physfirst to the last XIP's physlast. */
        uint32_t rom_va_base = 0xFFFFFFFFu;
        uint32_t rom_va_end  = 0u;
        for (const auto& xip : prim.xips) {
            const ParsedROMHDR& h = xip.toc.romhdr;
            if (h.physfirst < rom_va_base) rom_va_base = h.physfirst;
            if (h.physlast  > rom_va_end)  rom_va_end  = h.physlast;
        }

        rom_ = CachedWindowRomSpan(rom_va_base, rom_va_end);

        const ParsedROMHDR* hdr = prim.XipHeaderContaining(prim.entry_va);
        if (!hdr) {
            emu_.Get<Fatal>().Die("PhilipsVelo1PageTableBuilder: entry VA 0x%08X lies "
                                  "in no XIP", prim.entry_va);
        }

        RequireRamEndWithin(*hdr, kDramVaBase, kDramSize);

        card_bytes_ = emu_.Get<BoardConfig>().Enum(BoardConfigParamId::DramCard, kDramCardBytes);
        const auto& flash = emu_.Get<PhilipsVelo1InternalFlash>();
        mirror_pa_   = flash.ResetMirrorPa();
        mirror_size_ = uint32_t(flash.ResetMirrorBytes().size());
        const auto& rom_card = emu_.Get<PhilipsVelo1RomUpgradeCard>();
        card_kseg_  = rom_card.KsegWindow();
        card_kuseg_ = rom_card.KusegWindow();
        if (rom_card.Fitted() && rom_.pa_base < kPr31x00Cs1KsegEnd &&
            rom_.pa_base + rom_.size > kPr31x00Cs1KsegBase) {
            emu_.Get<Fatal>().UserError("rom.velo1_rom_upgrade_card needs the internal flash ROM "
                                        "as rom.primary; this primary ROM sits at PA 0x%08X, in "
                                        "the card's window", rom_.pa_base);
        }

        LOG(Boot, "PhilipsVelo1PageTableBuilder: ROM kva=0x%08X pa=0x%08X size=0x%X, "
                  "DRAM pa=0x%08X size=0x%X span=0x%X (ulRAMEnd=0x%08X), "
                  "DRAM card size=0x%X\n",
            rom_.va_base, rom_.pa_base, rom_.size, kDramPaBase, kDramSize,
            kDramDecodeSpan, hdr->ulRAMEnd, card_bytes_);
    }

    std::vector<DramRegion> CachedDramRegions() const override {
        std::vector<DramRegion> r = { { kDramVaBase, kDramPaBase, kDramSize } };
        if (card_bytes_) r.push_back({ kBank1VaBase, kBank1PaBase, card_bytes_ });
        return r;
    }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        std::vector<BackedRegion> r = {
            { kDramVaBase, kDramPaBase, kDramSize, MemoryKind::kRam, kDramDecodeSpan },
        };
        if (card_bytes_) {
            r.push_back({ kBank1VaBase, kBank1PaBase, card_bytes_, MemoryKind::kRam,
                          kBank1DecodeSpan });
        }
        r.push_back({ rom_.va_base, rom_.pa_base, rom_.size, MemoryKind::kRom });
        if (mirror_size_) {
            r.push_back({ Kseg0Va(mirror_pa_), mirror_pa_, mirror_size_, MemoryKind::kRom });
        }
        if (!card_kseg_.bytes.empty()) {
            r.push_back({ Kseg0Va(card_kseg_.pa), card_kseg_.pa,
                          uint32_t(card_kseg_.bytes.size()), MemoryKind::kRom });
            r.push_back({ 0u, card_kuseg_.pa, uint32_t(card_kuseg_.bytes.size()),
                          MemoryKind::kRom });
        }
        return r;
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        std::vector<DramRegion> r = { { kDramVaBase, kDramPaBase, kDramDecodeSpan } };
        if (card_bytes_) r.push_back({ kBank1VaBase, kBank1PaBase, kBank1DecodeSpan });
        r.push_back(rom_);
        if (mirror_size_) r.push_back({ Kseg0Va(mirror_pa_), mirror_pa_, mirror_size_ });
        if (!card_kseg_.bytes.empty()) {
            r.push_back({ Kseg0Va(card_kseg_.pa), card_kseg_.pa,
                          uint32_t(card_kseg_.bytes.size()) });
        }
        r.push_back({ kRegsVaBase, kRegsPaBase, kRegsSize });
        return r;
    }

private:
    static constexpr uint32_t Kseg0Va(uint32_t pa) { return cerfengine::MipsSeg::kKusegEnd | pa; }

    DramRegion      rom_{};
    uint32_t        card_bytes_  = 0;
    uint32_t        mirror_pa_   = 0;
    uint32_t        mirror_size_ = 0;
    Velo1CardWindow card_kseg_{};
    Velo1CardWindow card_kuseg_{};
};

}  /* namespace */

REGISTER_SERVICE_AS(PhilipsVelo1PageTableBuilder, PageTableBuilder);

}
