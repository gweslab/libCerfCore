#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../socs/msm8255/msm8255_gpio_bus.h"
#include "nokia_sea_ray_family_id.h"

#include <cstdint>
#include <iterator>

namespace cerfcore {

namespace {

constexpr uint32_t kRevisionStrapGpios[] = {85u, 37u, 5u};

class NokiaSeaRayHwRevisionStraps : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        const DbBoard& board =
            *emu_.Get<BoardDatabase>().FindBoard(emu_.Get<BoardContext>().GetBoardId());
        auto& bus = emu_.Get<Msm8255GpioBus>();
        for (uint32_t bit = 0; bit < std::size(kRevisionStrapGpios); ++bit) {
            bus.SetInputPin(kRevisionStrapGpios[bit], ((*board.hw_revision >> bit) & 1u) != 0u);
        }
    }
};

}

REGISTER_SERVICE(NokiaSeaRayHwRevisionStraps);

}
