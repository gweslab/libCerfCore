#include "ibm_workpad_z50_gpio_buffers.h"

#include <cerfcore/boards/board_context.h>
#include "../../core/board_config.h"
#include "../dram_card_param.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "ibm_workpad_z50_battery.h"
#include "ibm_workpad_z50_family_id.h"
#include "ibm_workpad_z50_member_straps.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <utility>

namespace cerfcore {

namespace {

constexpr uint8_t kCs0AcOffline      = 0x80u;
constexpr uint8_t kCs0KeyboardShift  = 5u;
constexpr uint8_t kCs0KeyboardMask   = 0x60u;
constexpr uint8_t kCs0NoExtensionRam = 0x18u;
constexpr uint8_t kCs0Extension32Mb  = 0x00u;
constexpr uint8_t kCs1ModemFitted    = 0x40u;
constexpr uint8_t kCs1MicEnable      = 0x02u;

uint8_t ExtensionRamStrap(uint32_t card_bytes) {
    return card_bytes == 0u ? kCs0NoExtensionRam : kCs0Extension32Mb;
}

}

REGISTER_SERVICE(IbmWorkpadZ50GpioBuffers);

bool IbmWorkpadZ50GpioBuffers::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
}

void IbmWorkpadZ50GpioBuffers::OnReady() {
    straps_  = &emu_.Get<IbmWorkpadZ50MemberStraps>();
    battery_ = &emu_.Get<IbmWorkpadZ50Battery>();
    extension_ram_strap_ = ExtensionRamStrap(emu_.Get<BoardConfig>().Enum(BoardConfigParamId::DramCard, kDramCardBytes));
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        cs0_out_.store(0u, std::memory_order_release);
        cs1_out_.store(0u, std::memory_order_release);
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { NotifyOutputListeners(); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t IbmWorkpadZ50GpioBuffers::Cs0Inputs() const {
    return static_cast<uint8_t>((battery_->OnAc() ? 0u : kCs0AcOffline) | extension_ram_strap_ |
                                ((straps_->KeyboardId() << kCs0KeyboardShift) & kCs0KeyboardMask));
}

uint8_t IbmWorkpadZ50GpioBuffers::Cs1Inputs() const {
    return straps_->ModemFitted() ? kCs1ModemFitted : 0u;
}

uint8_t IbmWorkpadZ50GpioBuffers::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kCs0Base;
    if (off == 0u) return Cs0Inputs();
    if (off == kCs1Offset) return Cs1Inputs();
    HaltUnsupportedAccess("IbmWorkpadZ50GpioBuffers ReadByte", addr, 0);
}

void IbmWorkpadZ50GpioBuffers::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kCs0Base;
    if (off == 0u) {
        cs0_out_.store(value, std::memory_order_release);
        return;
    }
    if (off == kCs1Offset) {
        cs1_out_.store(value, std::memory_order_release);
        NotifyOutputListeners();
        return;
    }
    HaltUnsupportedAccess("IbmWorkpadZ50GpioBuffers WriteByte", addr, value);
}

bool IbmWorkpadZ50GpioBuffers::MicEnabled() const {
    return (cs1_out_.load(std::memory_order_acquire) & kCs1MicEnable) != 0u;
}

void IbmWorkpadZ50GpioBuffers::RegisterOutputListener(std::function<void()> fn) {
    output_listeners_.push_back(std::move(fn));
}

void IbmWorkpadZ50GpioBuffers::NotifyOutputListeners() {
    for (const auto& fn : output_listeners_) fn();
}

void IbmWorkpadZ50GpioBuffers::SaveState(StateWriter& w) {
    w.Write("cs0_out", cs0_out_.load(std::memory_order_acquire));
    w.Write("cs1_out", cs1_out_.load(std::memory_order_acquire));
}

void IbmWorkpadZ50GpioBuffers::RestoreState(StateReader& r) {
    uint8_t v = 0;
    r.Read("cs0_out", v);
    cs0_out_.store(v, std::memory_order_release);
    r.Read("cs1_out", v);
    cs1_out_.store(v, std::memory_order_release);
}

}
