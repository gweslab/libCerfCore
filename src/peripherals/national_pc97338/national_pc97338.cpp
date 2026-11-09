#include "national_pc97338.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "../peripheral_dispatcher.h"
#include "national_pc97338_board.h"

namespace cerfcore {

REGISTER_SERVICE(NationalPc97338);

namespace {

constexpr uint8_t kIndexFer  = 0x00u;
constexpr uint8_t kIndexFar  = 0x01u;
constexpr uint8_t kIndexPtr  = 0x02u;
constexpr uint8_t kIndexSid  = 0x08u;
constexpr uint8_t kIndexScf2 = 0x40u;
constexpr uint8_t kIndexClk  = 0x51u;

constexpr uint8_t kClkReserved = 0xF0u;

}

bool NationalPc97338::ShouldRegister() {
    return emu_.TryGet<NationalPc97338Board>() != nullptr;
}

void NationalPc97338::OnReady() {
    isa_io_base_ = emu_.Get<NationalPc97338Board>().IsaIoBase();
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) { OnReset(kind); });
}

void NationalPc97338::OnReset(ResetLineKind kind) {
    if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
    index_ = 0u;
    index_id_pending_ = true;
    index_written_    = false;
    fer_   = kResetFer;
    far_   = kResetFar;
    ptr_   = kResetPtr;
    scf2_  = kResetScf2;
    if (kind != ResetLineKind::Rtc) return;
    clk_ = 0u;
    clk_source_written_ = false;
}

bool NationalPc97338::ClockRunning() const {
    if ((clk_ & kClkSource) == kClkSourceMultiplier14 || (clk_ & kClkSource) == kClkSourceMultiplier24)
        return (clk_ & kClkMultiplier) != 0u;
    return true;
}

uint8_t NationalPc97338::ReadByte(uint32_t addr) {
    if (addr - MmioBase() == 1u) return ReadConfig(index_);
    return ReadIndex(addr);
}

uint8_t NationalPc97338::ReadIndex(uint32_t addr) {
    if (index_id_pending_) {
        index_id_pending_ = false;
        return kIndexId;
    }
    if (!index_written_) return 0u;
    if ((index_ & kIndexReservedReadZero) == 0u) return static_cast<uint8_t>(index_ & kIndexReadable);
    emu_.Get<Fatal>().Die("PC97338: INDEX register read at 0x%08X after index 0x%02X was written", addr, index_);
}

void NationalPc97338::WriteByte(uint32_t addr, uint8_t value) {
    if (addr - MmioBase() == 0u) {
        index_ = value;
        index_id_pending_ = false;
        index_written_    = true;
        return;
    }
    if ((ptr_ & kPtrLock) != 0u) return;
    WriteConfig(index_, value);
}

uint8_t NationalPc97338::ReadConfig(uint8_t index) {
    switch (index) {
        case kIndexFer:  return fer_;
        case kIndexFar:  return far_;
        case kIndexPtr:  return ptr_;
        case kIndexSid:  return kSid;
        case kIndexScf2: return scf2_;
        case kIndexClk:  return static_cast<uint8_t>(clk_ | ((clk_ & kClkMultiplier) != 0u ? kClkValid : 0u));
        default: break;
    }
    emu_.Get<Fatal>().Die("PC97338: configuration register 0x%02X read", index);
}

void NationalPc97338::WriteConfig(uint8_t index, uint8_t value) {
    switch (index) {
        case kIndexFer:
            if ((value & kFerUnmodeled) != 0u)
                emu_.Get<Fatal>().Die("PC97338: FER write 0x%02X enables SCC2 or the FDC", value);
            fer_ = static_cast<uint8_t>(value & kFerDefined);
            return;
        case kIndexFar:
            if (value != far_) emu_.Get<Fatal>().Die("PC97338: FAR write 0x%02X moves 0x%02X", value, far_);
            return;
        case kIndexPtr:
            if ((value & kPtrUnmodeled) != 0u)
                emu_.Get<Fatal>().Die("PC97338: PTR write 0x%02X sets power down, bit 2, LPT2 IRQ7 or SCC1 test mode",
                                      value);
            if ((value & kPtrExtendedParallel) == 0u && parallel_irq_enabled_())
                emu_.Get<Fatal>().Die("PC97338: PTR write 0x%02X selects compatible SPP with the parallel port "
                                      "interrupt enabled", value);
            ptr_ = static_cast<uint8_t>(value & kPtrStored);
            return;
        case kIndexScf2:
            if ((value & (kScf2Vld | kScf2Scc2BankSelect)) != 0u || (value & kScf2Scc2NormalPower) == 0u)
                emu_.Get<Fatal>().Die("PC97338: SCF2 write 0x%02X changes the FDC VLD bits or SCC2", value);
            scf2_ = static_cast<uint8_t>(kScf2Scc2NormalPower | (value & kScf2Scc1BankSelect));
            return;
        case kIndexClk:
            WriteClk(value);
            return;
        default: break;
    }
    emu_.Get<Fatal>().Die("PC97338: configuration register 0x%02X write 0x%02X", index, value);
}

void NationalPc97338::WriteClk(uint8_t value) {
    if ((value & kClkReserved) != 0u) emu_.Get<Fatal>().Die("PC97338: CLK write 0x%02X sets reserved bits", value);
    uint8_t source = static_cast<uint8_t>(clk_ & kClkSource);
    if (!clk_source_written_) {
        source = static_cast<uint8_t>(value & kClkSource);
        if (source == kClkSourceReserved) emu_.Get<Fatal>().Die("PC97338: CLK write 0x%02X selects the reserved clock source", value);
        clk_source_written_ = true;
    }
    clk_ = static_cast<uint8_t>((value & kClkMultiplier) | source);
}

void NationalPc97338::SaveState(StateWriter& w) {
    w.Write("index", index_);
    w.Write<uint8_t>("index_id_pending", index_id_pending_ ? 1u : 0u);
    w.Write<uint8_t>("index_written", index_written_ ? 1u : 0u);
    w.Write("fer", fer_);
    w.Write("far", far_);
    w.Write("ptr", ptr_);
    w.Write("scf2", scf2_);
    w.Write("clk", clk_);
    w.Write<uint8_t>("clk_source_written", clk_source_written_ ? 1u : 0u);
}

void NationalPc97338::RestoreState(StateReader& r) {
    r.Read("index", index_);
    uint8_t id_pending = 0u;
    uint8_t written = 0u;
    r.Read("index_id_pending", id_pending);
    r.Read("index_written", written);
    index_id_pending_ = id_pending != 0u;
    index_written_    = written != 0u;
    r.Read("fer", fer_);
    r.Read("far", far_);
    r.Read("ptr", ptr_);
    r.Read("scf2", scf2_);
    r.Read("clk", clk_);
    uint8_t source_written = 0u;
    r.Read("clk_source_written", source_written);
    clk_source_written_ = source_written != 0u;
}

}
