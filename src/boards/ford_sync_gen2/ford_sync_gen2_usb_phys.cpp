#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../core/board_parts.h"
#include "../../peripherals/smsc_usb3317/smsc_usb3317_part_id.h"
#include "../../peripherals/smsc_usb3317/usb3317_ulpi_phy.h"
#include "../../socs/chipidea_ulpi_phys.h"
#include "ford_sync_gen2_family_id.h"

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kCores   = 4u;
constexpr uint32_t kOtgCore = 0u;

class FordSyncGen2UsbPhys : public ChipIdeaUlpiPhys {
public:
    using ChipIdeaUlpiPhys::ChipIdeaUlpiPhys;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::FordSyncGen2 &&
               emu_.Get<BoardParts>().Has(PartId::SmscUsb3317);
    }

    void OnReady() override {
        phys_[kOtgCore].SetVbus(UlpiVbus::kVbusValid);
        PowerOn();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) PowerOn();
        });
    }

    UlpiPhy* Phy(uint32_t core) override { return core < kCores ? &phys_[core] : nullptr; }

private:
    void PowerOn() {
        for (Usb3317UlpiPhy& phy : phys_) phy.PowerOn();
    }

    std::array<Usb3317UlpiPhy, kCores> phys_{};
};

}

REGISTER_SERVICE_AS(FordSyncGen2UsbPhys, ChipIdeaUlpiPhys);

}
