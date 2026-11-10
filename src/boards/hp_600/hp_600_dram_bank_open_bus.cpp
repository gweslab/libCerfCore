#include "hp_600_dram_bank_open_bus.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/sh77xx/sh77xx_area3_dram.h"
#include "../page_table_builder.h"
#include "hp_600_family_id.h"

namespace cerfcore {

uint32_t Hp600DramBankOpenBus::BackedBytes() const {
    for (const BackedRegion& r : emu_.Get<PageTableBuilder>().BackedMemoryRegions()) {
        if (r.pa_base == BankPaBase()) return r.decode_span != 0u ? r.decode_span : r.size;
    }
    return 0u;
}

uint32_t Hp600DramBankOpenBus::MmioBase() const { return BankPaBase() + BackedBytes(); }

uint32_t Hp600DramBankOpenBus::MmioSize() const { return kSh77xxRasWindowBytes - BackedBytes(); }

std::string_view Hp600DramBankOpenBus::WindowBoardFamilyId() const { return BoardFamilyId::Hp600; }

bool Hp600DramBankOpenBus::WindowOnBoard(const BoardContext& bd) const {
    return bd.GetSocId() == ProbingSocId() && BackedBytes() < kSh77xxRasWindowBytes;
}

}
