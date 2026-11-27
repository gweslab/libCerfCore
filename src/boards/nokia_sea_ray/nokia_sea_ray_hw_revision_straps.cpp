#include "nokia_sea_ray_hw_revision_straps.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../core/board_config.h"
#include "../../socs/msm8255/msm8255_gpio_bus.h"
#include "../hw_revision_param_id.h"
#include "nokia_sea_ray_family_id.h"

#include <cstdint>
#include <iterator>
#include <string_view>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint32_t kRevisionStrapGpios[] = {85u, 37u, 5u};

constexpr std::pair<std::string_view, uint32_t> kRevisions[] = {{"2", 2u}, {"3", 3u}, {"4", 4u}};

}

bool NokiaSeaRayHwRevisionStraps::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
}

void NokiaSeaRayHwRevisionStraps::OnReady() {
    const uint32_t revision = Revision();
    auto&          bus      = emu_.Get<Msm8255GpioBus>();
    for (uint32_t bit = 0; bit < std::size(kRevisionStrapGpios); ++bit) {
        bus.SetInputPin(kRevisionStrapGpios[bit], ((revision >> bit) & 1u) != 0u);
    }
}

uint32_t NokiaSeaRayHwRevisionStraps::Revision() const {
    return emu_.Get<BoardConfig>().Enum(BoardConfigParamId::HwRevision, kRevisions);
}

REGISTER_SERVICE(NokiaSeaRayHwRevisionStraps);

}
