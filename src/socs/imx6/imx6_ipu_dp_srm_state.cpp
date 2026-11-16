#include "imx6_ipu_dp_srm_state.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_ipu_cpmem.h"
#include "imx6_ipu_register_map.h"
#include "imx6_id.h"

namespace cerfcore {

bool Imx6IpuDpSrmState::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

void Imx6IpuDpSrmState::UpdateFromSrm() {
    auto& memory = emu_.Get<Imx6IpuCpmem>();
    const uint32_t shadow = memory.MmioBase() + 0x00040000u;
    registers_[0] = memory.ReadWord(shadow);
    registers_[1] = memory.ReadWord(shadow + 4u);
    registers_[2] = memory.ReadWord(shadow + 8u);
}

void Imx6IpuDpSrmState::Reset() { registers_.fill(0u); }

uint32_t Imx6IpuDpSrmState::Register(uint32_t offset) const {
    return registers_[(offset - imx6_ipu::kOffDpComConfSync) >> 2];
}

void Imx6IpuDpSrmState::SaveState(StateWriter& writer) {
    writer.WriteBytes("registers", registers_.data(), registers_.size() * sizeof(registers_[0]));
}

void Imx6IpuDpSrmState::RestoreState(StateReader& reader) {
    reader.ReadBytes("registers", registers_.data(), registers_.size() * sizeof(registers_[0]));
}

REGISTER_SERVICE(Imx6IpuDpSrmState);

}
