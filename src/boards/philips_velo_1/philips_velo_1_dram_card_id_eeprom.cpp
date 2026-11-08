#include "philips_velo_1_dram_card_id_eeprom.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "philips_velo_1_dram_card_profile.h"
#include "philips_velo_1_family_id.h"

namespace cerfcore {

namespace {

constexpr uint8_t  kDeviceAddress    = 0x50u;
constexpr uint16_t kPageBytesUnknown = 0u;
constexpr uint8_t  kSignature        = 0x99u;
constexpr uint8_t  kTupleCount       = 1u;
constexpr uint8_t  kTupleType        = 2u;
constexpr uint8_t  kDevicePresent    = 0x01u;
constexpr uint8_t  kRefreshByte      = 127u;

constexpr bool CardGeometryHolds() {
    for (const PhilipsVelo1DramCardProfile& p : kPhilipsVelo1DramCards) {
        if ((uint64_t{2} << (p.row_bits + p.column_bits)) != (uint64_t{p.megabytes} << 20)) {
            return false;
        }
    }
    return true;
}
static_assert(CardGeometryHolds());

const PhilipsVelo1DramCardProfile* CardProfile(uint32_t card_bytes) {
    for (const PhilipsVelo1DramCardProfile& p : kPhilipsVelo1DramCards) {
        if ((uint32_t{p.megabytes} << 20) == card_bytes) return &p;
    }
    return nullptr;
}

}

REGISTER_SERVICE(PhilipsVelo1DramCardIdEeprom);

bool PhilipsVelo1DramCardIdEeprom::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::PhilipsVelo1 &&
           CardProfile(bd->DramCardBytes()) != nullptr;
}

void PhilipsVelo1DramCardIdEeprom::FillCells() {
    const PhilipsVelo1DramCardProfile& card = *CardProfile(emu_.Get<BoardContext>().DramCardBytes());
    auto& id_rom = Cells();
    id_rom.fill(0);
    id_rom[16]  = kSignature;
    id_rom[59]  = kTupleCount;
    id_rom[64]  = kTupleType;
    id_rom[67]  = static_cast<uint8_t>(card.megabytes - 1u);
    id_rom[96]  = card.row_bits;
    id_rom[97]  = card.column_bits;
    id_rom[98]  = kDevicePresent;
    id_rom[101] = kRefreshByte;
}

uint8_t PhilipsVelo1DramCardIdEeprom::DeviceAddress() const { return kDeviceAddress; }

uint16_t PhilipsVelo1DramCardIdEeprom::PageBytes() const { return kPageBytesUnknown; }

const char* PhilipsVelo1DramCardIdEeprom::PartName() const {
    return "Philips Velo 1 DRAM card ID EEPROM";
}

}
