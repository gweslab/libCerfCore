#include "samsung_smdk2410_fn_key.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "samsung_smdk2410_family_id.h"

namespace cerfcore {

namespace {

class SamsungSmdk2410DefaultFnKey : public SamsungSmdk2410FnKey {
public:
    using SamsungSmdk2410FnKey::SamsungSmdk2410FnKey;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    uint8_t Scancode() const override { return 0x21; }
    bool    HasApp1Key() const override { return false; }
};

}

REGISTER_SERVICE_AS_FALLBACK(SamsungSmdk2410DefaultFnKey, SamsungSmdk2410FnKey);

}
