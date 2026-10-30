#include "samsung_smdk2410_fn_key.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "devemu_1st_gen_id.h"
#include "devemu_1st_gen_smartphone_id.h"
#include "devemu_1st_gen_vga_id.h"

namespace cerfcore {

namespace {

class SamsungSmdk2410FirstGenFnKey : public SamsungSmdk2410FnKey {
public:
    using SamsungSmdk2410FnKey::SamsungSmdk2410FnKey;

    bool ShouldRegister() override {
        const std::string_view id = emu_.Get<BoardContext>().GetBoardId();
        return id == BoardId::Devemu1stGen || id == BoardId::Devemu1stGenSmartphone || id == BoardId::Devemu1stGenVga;
    }

    uint8_t Scancode() const override { return 0x63; }
    bool    HasApp1Key() const override { return true; }
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410FirstGenFnKey, SamsungSmdk2410FnKey);

}
