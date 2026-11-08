#include "../mips_kseg_page_table_builder.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/cpu/memory_kind.h>
#include "ibm_workpad_z50_dram_layout.h"
#include "ibm_workpad_z50_family_id.h"
#include "ibm_workpad_z50_member_straps.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace {

constexpr DramRegion kDram              = { 0x80000000u, 0x00000000u, kIbmWorkpadZ50DramBankBytes };
constexpr uint32_t   kKseg0Base         = 0x80000000u;
constexpr uint32_t   kChecksumWordBytes = 4u;

class IbmWorkpadZ50PageTableBuilder : public MipsKsegPageTableBuilder {
public:
    using MipsKsegPageTableBuilder::MipsKsegPageTableBuilder;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    void OnReady() override {
        const auto& straps = emu_.Get<IbmWorkpadZ50MemberStraps>();
        card_ = Kseg0RomSpan(kKseg0Base + straps.RomCardPa(),
                             kKseg0Base + kIbmWorkpadZ50RomSpaceEndPa);
        dram_ = { kDram };
        const uint32_t card_bytes = emu_.Get<BoardContext>().DramCardBytes();
        for (uint32_t off = 0; off < card_bytes; off += kIbmWorkpadZ50DramBankBytes) {
            const uint32_t pa = kIbmWorkpadZ50ExpansionDramPa + off;
            dram_.push_back({ kKseg0Base + pa, pa, kIbmWorkpadZ50DramBankBytes });
        }
    }

    std::vector<DramRegion> CachedDramRegions() const override { return dram_; }

    std::vector<BackedRegion> BackedMemoryRegions() const override {
        std::vector<BackedRegion> r;
        for (const DramRegion& d : dram_) r.push_back({ d.va_base, d.pa_base, d.size, MemoryKind::kRam });
        r.push_back({ card_.va_base + kChecksumWordBytes, card_.pa_base + kChecksumWordBytes,
                      card_.size - kChecksumWordBytes, MemoryKind::kRom });
        return r;
    }

    std::vector<DramRegion> MappedVaSpans() const override {
        std::vector<DramRegion> r = dram_;
        r.push_back(card_);
        return r;
    }

private:
    std::vector<DramRegion> dram_;
    DramRegion              card_{};
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50PageTableBuilder, PageTableBuilder);

}
