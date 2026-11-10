#include "philips_velo_1_internal_flash.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/boot/rom_parser_service.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfengine/mips/mips_mmu.h>
#include "../../cpu/r3000/r3900_reset_vector.h"
#include "../../socs/pr31x00/pr31x00_chip_selects.h"
#include "philips_velo_1_family_id.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kFlashBytes = 0x00800000u;

}

REGISTER_SERVICE(PhilipsVelo1InternalFlash);

bool PhilipsVelo1InternalFlash::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1;
}

void PhilipsVelo1InternalFlash::OnReady() {
    const ParsedRom& prim = emu_.Get<RomParserService>().Primary();
    const uint32_t image_pa = cerfengine::MipsSeg::UnmappedPa(prim.flat_base_va);
    const uint32_t reset_pa = cerfengine::MipsSeg::UnmappedPa(kR3900ResetVector);
    if (image_pa < kPr31x00Cs0KsegBase ||
        uint64_t(image_pa) + prim.flat.size() > reset_pa) {
        return;
    }
    const uint32_t first = (reset_pa - image_pa) % kFlashBytes;
    if (first >= prim.flat.size()) return;
    const size_t length = std::min<size_t>(kPr31x00Cs0KsegEnd - reset_pa,
                                           prim.flat.size() - first);
    mirror_pa_ = reset_pa;
    mirror_    = std::span<const uint8_t>(prim.flat).subspan(first, length);
}

}
