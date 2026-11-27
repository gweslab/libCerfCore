#include "adreno_a2xx_interrupts.h"

#include "adreno_a2xx_regs.h"
#include "adreno_a2xx_socs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

using namespace adreno_a2xx_regs;

namespace {

constexpr uint32_t kMhDefined   = 0x00000007u;
constexpr uint32_t kCpDefined   = 0xEF880000u;
constexpr uint32_t kRbbmDefined = 0x00080003u;
constexpr uint32_t kSqDefined   = 0x00000003u;

constexpr uint32_t kMasterMh   = 0x00000020u;
constexpr uint32_t kMasterSq   = 0x04000000u;
constexpr uint32_t kMasterCp   = 0x40000000u;
constexpr uint32_t kMasterRbbm = 0x80000000u;

constexpr uint32_t kCpBlock = 1u;

}

const AdrenoA2xxInterrupts::Block AdrenoA2xxInterrupts::kBlocks[kBlockCount] = {
    {kIdxMhInterruptMask, kIdxMhInterruptStatus, kIdxMhInterruptClear, kMhDefined, kMasterMh},
    {kIdxCpIntCntl, kIdxCpIntStatus, kIdxCpIntAck, kCpDefined, kMasterCp},
    {kIdxRbbmIntCntl, kIdxRbbmIntStatus, kIdxRbbmIntAck, kRbbmDefined, kMasterRbbm},
    {kIdxSqIntCntl, kIdxSqIntStatus, kIdxSqIntAck, kSqDefined, kMasterSq},
};

REGISTER_SERVICE(AdrenoA2xxInterrupts);

bool AdrenoA2xxInterrupts::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

bool AdrenoA2xxInterrupts::Owns(uint32_t idx) const {
    if (idx == kIdxMasterIntSignal) return true;
    for (const Block& block : kBlocks) {
        if (idx == block.mask_idx || idx == block.status_idx || idx == block.ack_idx) return true;
    }
    return false;
}

uint32_t AdrenoA2xxInterrupts::MasterSignal() const {
    uint32_t master = 0u;
    for (uint32_t i = 0; i < kBlockCount; ++i) {
        if ((status_[i] & mask_[i]) != 0u) master |= kBlocks[i].master_bit;
    }
    return master;
}

bool AdrenoA2xxInterrupts::Asserted() const { return MasterSignal() != 0u; }

uint32_t AdrenoA2xxInterrupts::Read(uint32_t idx) const {
    if (idx == kIdxMasterIntSignal) return MasterSignal();
    for (uint32_t i = 0; i < kBlockCount; ++i) {
        if (idx == kBlocks[i].mask_idx) return mask_[i];
        if (idx == kBlocks[i].status_idx) return status_[i];
    }
    emu_.Get<Fatal>().Die("GPU interrupt register 0x%03X read is not modeled", idx);
}

void AdrenoA2xxInterrupts::Write(uint32_t idx, uint32_t value, uint32_t address) {
    for (uint32_t i = 0; i < kBlockCount; ++i) {
        const Block& block = kBlocks[i];
        if (idx == block.ack_idx) {
            status_[i] &= ~value;
            return;
        }
        if (idx != block.mask_idx) continue;
        if ((value & ~block.defined) != 0u)
            emu_.Get<Fatal>().Die("GPU interrupt mask write at 0x%08X sets undefined bits "
                                  "(value 0x%08X)", address, value);
        mask_[i] = value;
        return;
    }
    emu_.Get<Fatal>().Die("GPU interrupt register write at 0x%08X (value 0x%08X) is not modeled",
                          address, value);
}

void AdrenoA2xxInterrupts::RaiseCp(uint32_t bits, uint32_t address) {
    if (bits == 0u || (bits & ~kCpDefined) != 0u)
        emu_.Get<Fatal>().Die("GPU CP interrupt packet at 0x%08X raises bits 0x%08X", address, bits);
    status_[kCpBlock] |= bits;
}

void AdrenoA2xxInterrupts::PowerOnReset() {
    for (uint32_t i = 0; i < kBlockCount; ++i) {
        status_[i] = 0u;
        mask_[i]   = 0u;
    }
}

void AdrenoA2xxInterrupts::SaveState(StateWriter& w) const {
    w.WriteBytes("gpu_interrupt_status", status_, sizeof(status_));
    w.WriteBytes("gpu_interrupt_mask", mask_, sizeof(mask_));
}

void AdrenoA2xxInterrupts::RestoreState(StateReader& r) {
    r.ReadBytes("gpu_interrupt_status", status_, sizeof(status_));
    r.ReadBytes("gpu_interrupt_mask", mask_, sizeof(mask_));
}

}
