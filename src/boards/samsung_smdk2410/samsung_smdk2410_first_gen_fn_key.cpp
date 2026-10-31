#include "samsung_smdk2410_fn_key.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "devemu_gen1_id.h"
#include "devemu_gen1_sp_id.h"
#include "devemu_gen1_sp_qvga_id.h"
#include "devemu_gen1_vga_id.h"
#include "devemu_gen1_wince_svga_id.h"

namespace cerfcore {

namespace {

class SamsungSmdk2410FirstGenFnKey : public SamsungSmdk2410FnKey {
public:
    using SamsungSmdk2410FnKey::SamsungSmdk2410FnKey;

    bool ShouldRegister() override {
        const std::string_view id = emu_.Get<BoardContext>().GetBoardId();
        return id == BoardId::DevemuGen1 || id == BoardId::DevemuGen1Vga || id == BoardId::DevemuGen1Sp ||
               id == BoardId::DevemuGen1SpQvga || id == BoardId::DevemuGen1WinceSvga;
    }

    uint8_t Scancode() const override { return 0x63; }
    bool    HasApp1Key() const override { return true; }
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410FirstGenFnKey, SamsungSmdk2410FnKey);

}
