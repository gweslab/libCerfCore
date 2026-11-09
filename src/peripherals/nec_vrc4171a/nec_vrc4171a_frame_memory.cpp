#include "nec_vrc4171a_frame_memory.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../peripheral_dispatcher.h"
#include "nec_vrc4171a_lcd_board.h"

#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(NecVrc4171aFrameMemory);

bool NecVrc4171aFrameMemory::ShouldRegister() {
    return emu_.TryGet<NecVrc4171aLcdBoard>() != nullptr;
}

void NecVrc4171aFrameMemory::OnReady() {
    const auto& board = emu_.Get<NecVrc4171aLcdBoard>();
    base_ = board.FrameMemoryBase();
    memory_.assign(board.FrameMemoryBytes(), 0u);
    SetDecodedBytes(MmioSize());
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
        if (kind == ResetLineKind::Rtc) std::fill(memory_.begin(), memory_.end(), static_cast<uint8_t>(0u));
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void NecVrc4171aFrameMemory::SetDecodedBytes(uint32_t bytes) {
    mask_.store(bytes - 1u, std::memory_order_release);
}

uint8_t* NecVrc4171aFrameMemory::At(uint32_t addr) {
    return memory_.data() + ((addr - base_) & mask_.load(std::memory_order_acquire));
}

uint8_t NecVrc4171aFrameMemory::ReadByte(uint32_t addr) { return *At(addr); }

uint16_t NecVrc4171aFrameMemory::ReadHalf(uint32_t addr) { return cerf::le::U16(At(addr)); }

uint64_t NecVrc4171aFrameMemory::ReadDword(uint32_t addr) { return cerf::le::U64(At(addr)); }

void NecVrc4171aFrameMemory::WriteByte(uint32_t addr, uint8_t value) { *At(addr) = value; }

void NecVrc4171aFrameMemory::WriteHalf(uint32_t addr, uint16_t value) { cerf::le::Put16(At(addr), value); }

void NecVrc4171aFrameMemory::WriteDword(uint32_t addr, uint64_t value) { cerf::le::Put64(At(addr), value); }

void NecVrc4171aFrameMemory::SaveState(StateWriter& w) {
    w.WriteBytes("frame_memory", memory_.data(), memory_.size());
}

void NecVrc4171aFrameMemory::RestoreState(StateReader& r) {
    r.ReadBytes("frame_memory", memory_.data(), memory_.size());
}

}
