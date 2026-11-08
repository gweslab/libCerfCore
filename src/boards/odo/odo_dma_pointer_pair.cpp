#include "odo_dma_pointer_pair.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "odo_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kDmaPairSize = 0x08u;
constexpr uint32_t kDramPaBase  = 0x0C000000u;

}

bool OdoDmaPointerPair::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::Odo;
}

void OdoDmaPointerPair::OnReady() {
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> lk(state_mutex_);
        dma_low_  = 0;
        dma_high_ = 0;
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t OdoDmaPointerPair::MmioSize() const { return kDmaPairSize; }

uint32_t OdoDmaPointerPair::GetEffectivePa() {
    std::lock_guard<std::mutex> lk(state_mutex_);
    const uint32_t chip_addr = (static_cast<uint32_t>(dma_high_ & 0xFFu) << 16) |
                               static_cast<uint32_t>(dma_low_);
    return kDramPaBase + chip_addr;
}

void OdoDmaPointerPair::Store(uint32_t off, uint16_t value) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    if (off == kSlotDmaLow) dma_low_  = value;
    else                    dma_high_ = value;
}

uint16_t OdoDmaPointerPair::Load(uint32_t off) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    return off == kSlotDmaLow ? dma_low_ : dma_high_;
}

void OdoDmaPointerPair::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    w.Write("dma_low", dma_low_);
    w.Write("dma_high", dma_high_);
}

void OdoDmaPointerPair::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(state_mutex_);
    r.Read("dma_low", dma_low_);
    r.Read("dma_high", dma_high_);
}

}
