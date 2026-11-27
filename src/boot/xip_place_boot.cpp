#include "boot_mode.h"

#include <cerfcore/boot/rom_parser_service.h>

#include <cerfcore/boards/board_context.h>
#include "flat_container_placing_mode_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/core/fatal.h>
#include "../boards/page_table_builder.h"

namespace cerfcore {

namespace {

class XipPlaceBoot : public BootMode {
public:
    using BootMode::BootMode;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetRomPlacingMode()
            == RomPlacingModeId::FlatContainer;
    }

    void OnReady() override {
        BootMode::OnReady();
        if (!emu_.Get<RomParserService>().Ok()) {
            emu_.Get<Fatal>().UserError("device '%s': the ROM holds no image CERF can boot",
                                        emu_.Get<DeviceConfig>().device_name.c_str());
        }
    }

    /* ARM ARM DDI 0406C.c A2.3.2 BXWritePC(), p. A2-47: "BranchTo(address<31:1>
       :'0')" - bit<0> is the instruction-set selector, not part of the
       address. */
    uint32_t ColdEntryPa() override {
        return KernelEntryPa(emu_.Get<RomParserService>().EntryVa());
    }

    /* ARM ARM DDI 0406C.c A2.3.2 BXWritePC(), p. A2-47: "if address<0> == '1'
       then SelectInstrSet(InstrSet_Thumb)". */
    bool ColdEntryThumb() override {
        return KernelEntryThumb(emu_.Get<RomParserService>().EntryVa());
    }

    uint32_t ColdStackPa() override {
        return emu_.Get<PageTableBuilder>().InitStackTopPa();
    }
};

}  /* namespace */

REGISTER_SERVICE_AS(XipPlaceBoot, BootMode);

}
