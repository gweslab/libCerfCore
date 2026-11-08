#include "ibm_workpad_z50_board_eeprom.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_family_id.h"

namespace cerfcore {

namespace {

constexpr uint8_t  kDeviceAddress = 0x50u;
constexpr uint16_t kPageBytes     = 16u;
constexpr uint8_t  kBlankCell     = 0xFFu;

}

REGISTER_SERVICE(IbmWorkpadZ50BoardEeprom);

bool IbmWorkpadZ50BoardEeprom::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
}

void IbmWorkpadZ50BoardEeprom::FillCells() {
    Cells().fill(kBlankCell);
}

uint8_t IbmWorkpadZ50BoardEeprom::DeviceAddress() const { return kDeviceAddress; }

uint16_t IbmWorkpadZ50BoardEeprom::PageBytes() const { return kPageBytes; }

const char* IbmWorkpadZ50BoardEeprom::PartName() const { return "z50 board EEPROM"; }

}
