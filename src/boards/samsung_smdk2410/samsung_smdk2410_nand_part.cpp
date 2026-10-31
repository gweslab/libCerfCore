#include "../../socs/s3c2410/s3c2410_nand_part.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/nand/erased_small_page_nand_store.h"
#include "../../peripherals/nand/small_page_nand_flash.h"
#include "samsung_smdk2410_family_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint8_t kNandId[] = {0xECu, 0x76u};

constexpr SmallPageNandGeometry kNandGeometry = {
    4096u, 3u, kNandId, sizeof(kNandId), false,
};

class SamsungSmdk2410NandPart : public S3c2410NandPart {
public:
    using S3c2410NandPart::S3c2410NandPart;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SamsungSmdk2410;
    }

    const SmallPageNandGeometry& Geometry() const override { return kNandGeometry; }
    SmallPageNandStore&          Store()          override { return store_; }

private:
    ErasedSmallPageNandStore store_{emu_, "SMDK2410 NAND"};
};

}

REGISTER_SERVICE_AS(SamsungSmdk2410NandPart, S3c2410NandPart);

}
