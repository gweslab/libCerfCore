#include "stm32f2_mcu.h"

#include "../../core/unreachable.h"
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace stm32f2 {

constexpr uint8_t kSramPowerOnByte = 0u;

Mcu::Mcu(McuHost& host)
    : host_(host), flash_(kFlashBytes, 0xFFu), sram_(kSramBytes, kSramPowerOnByte), otp_(kOtpBytes, 0xFFu) {
    core_.MapMemory(kFlashBase, kFlashBytes, flash_.data(), false);
    core_.MapMemory(kSramBase, kSramBytes, sram_.data(), true);
    core_.TrackWrites(kSramBase, sram_dirty_.data());
    static_assert(cortex_m3::kExcExternalBase + kInterruptLines <= cortex_m3::kMaxExceptions);
    core_.SetInterruptLines(kInterruptLines);
}

void Mcu::AddPeripheral(uint32_t base, Peripheral* peripheral) {
    peripherals_.push_back({base, peripheral});
}

void Mcu::GateClock(uint32_t base, std::function<bool()> enabled) {
    for (Mapping& m : peripherals_) {
        if (m.base == base) m.clock_enabled = std::move(enabled);
    }
}

void Mcu::Start(uint32_t vector_table) {
    core_.Reset(*this, vector_table);
}

void Mcu::RunUntil(uint64_t cycle) {
    while (core_.Cycles() < cycle && reset_flags_ == 0u) {
        uint64_t limit = std::min(cycle, next_event_);
        if (reset_deadline_ > core_.Cycles()) limit = std::min(limit, reset_deadline_);
        core_.Run(*this, limit);
        if (core_.Cycles() >= next_event_ || core_.Cycles() >= reset_deadline_) FlushTicks();
    }
}

void Mcu::CatchUp(Mapping& m) {
    const uint64_t now = core_.Cycles();
    uint64_t behind = now - m.ticked;
    m.ticked = now;
    if (Unclocked(m)) return;
    for (; behind != 0u; behind -= std::min<uint64_t>(behind, UINT32_MAX))
        m.peripheral->Tick(static_cast<uint32_t>(std::min<uint64_t>(behind, UINT32_MAX)));
}

uint32_t Mcu::UnclockedValue(const Mapping& m, uint32_t offset, uint32_t address) {
    uint32_t value = 0u;
    if (!m.peripheral->Peek(offset, &value))
        Fault("STM32F2: access to a register with read side effects while its clock is disabled", address);
    return value;
}

bool Mcu::EnterDeepSleep() {
    Fault("STM32F2: Stop mode is not modelled", core_.ProgramCounter());
    CERF_UNREACHABLE();
}

void Mcu::ExitDeepSleep() {
    Fault("STM32F2: Stop mode is not modelled", core_.ProgramCounter());
}

void Mcu::ClockChanged() {
    for (Mapping& m : peripherals_) m.peripheral->ClockChanged();
    RecomputeNextEvent();
}

uint64_t Mcu::Synchronize() {
    FlushTicks();
    return reset_deadline_ > core_.Cycles() ? std::min(next_event_, reset_deadline_) : next_event_;
}

void Mcu::FlushTicks() {
    for (Mapping& m : peripherals_) CatchUp(m);
    RecomputeNextEvent();
}

uint64_t Mcu::EventCycle(uint32_t base) const {
    for (const Mapping& m : peripherals_) {
        if (m.base == base) return EventOf(m);
    }
    return UINT64_MAX;
}

uint64_t Mcu::EventOf(const Mapping& m) const {
    if (Unclocked(m)) return UINT64_MAX;
    const uint64_t cycles = m.peripheral->CyclesToEvent();
    return cycles == UINT64_MAX ? UINT64_MAX : m.ticked + cycles;
}

void Mcu::RecomputeNextEvent() {
    next_event_ = UINT64_MAX;
    for (const Mapping& m : peripherals_) next_event_ = std::min(next_event_, EventOf(m));
}

uint8_t* Mcu::Memory(uint32_t address, uint32_t size, bool write) {
    if (address >= kSramBase && address - kSramBase <= kSramBytes - size) return &sram_[address - kSramBase];
    if (write) return nullptr;
    if (address >= kFlashBase && address - kFlashBase <= kFlashBytes - size) return &flash_[address - kFlashBase];
    if (address >= kOtpBase && address - kOtpBase <= kOtpBytes - size) return &otp_[address - kOtpBase];
    if (address <= kFlashBytes - size) return &flash_[address];
    return nullptr;
}

uint8_t* Mcu::ProgramTarget(uint32_t address, uint32_t size) {
    if (address >= kFlashBase && address - kFlashBase <= kFlashBytes - size) return &flash_[address - kFlashBase];
    if (address >= kOtpBase && address - kOtpBase <= kOtpBytes - size) return &otp_[address - kOtpBase];
    return nullptr;
}

bool Mcu::Unclocked(const Mapping& m) const {
    return m.clock_enabled && !m.clock_enabled();
}

Mcu::Mapping* Mcu::Find(uint32_t address, uint32_t* offset) {
    for (Mapping& m : peripherals_) {
        if (address >= m.base && address - m.base < kPeripheralWindow) {
            *offset = address - m.base;
            return &m;
        }
    }
    return nullptr;
}

uint32_t Mcu::Read(uint32_t address, uint32_t size) {
    if (const uint8_t* p = Memory(address, size, false)) {
        uint32_t value = 0u;
        std::memcpy(&value, p, size);
        return value;
    }
    uint32_t offset = 0u;
    if (Mapping* m = Find(address, &offset)) {
        CatchUp(*m);
        const uint32_t mask = size == 4u ? 0xFFFFFFFFu : (1u << (size * 8u)) - 1u;
        if (Unclocked(*m)) {
            if ((UnclockedValue(*m, offset, address) & mask) != 0u)
                Fault("STM32F2: non-zero read of a peripheral whose clock is disabled", address);
            return 0u;
        }
        return m->peripheral->Read(offset, size) & mask;
    }
    host_.McuFault("STM32F2: read of an unmodelled address", address, core_.ProgramCounter());
    CERF_UNREACHABLE();
}

void Mcu::Write(uint32_t address, uint32_t size, uint32_t value) {
    if (uint8_t* p = Memory(address, size, true)) {
        std::memcpy(p, &value, size);
        const uint32_t offset = address - kSramBase;
        for (uint32_t page = offset >> cortex_m3::Core::kDirtyPageShift;
             page <= (offset + size - 1u) >> cortex_m3::Core::kDirtyPageShift; ++page)
            sram_dirty_[page >> 6] |= uint64_t{1} << (page & 63u);
        return;
    }
    if (ProgramTarget(address, size) != nullptr) {
        flash_programmer_->Program(address, size, value);
        return;
    }
    uint32_t offset = 0u;
    if (Mapping* m = Find(address, &offset)) {
        CatchUp(*m);
        if (Unclocked(*m)) {
            const uint32_t mask = size == 4u ? 0xFFFFFFFFu : (1u << (size * 8u)) - 1u;
            if ((UnclockedValue(*m, offset, address) & mask) != (value & mask))
                Fault("STM32F2: a write that changes a peripheral whose clock is disabled", address);
            return;
        }
        m->peripheral->Write(offset, size, value);
        RecomputeNextEvent();
        return;
    }
    host_.McuFault("STM32F2: write of an unmodelled address", address, core_.ProgramCounter());
}

[[noreturn]] void Mcu::Fault(const char* what, uint32_t detail) {
    host_.McuFault(what, detail, core_.ProgramCounter());
    CERF_UNREACHABLE();
}

void Mcu::SaveState(StateWriter& writer, bool sram) const {
    core_.SaveState(writer);
    if (sram) writer.WriteBytes("stm32f2_sram", sram_.data(), sram_.size());
    writer.Write("stm32f2_reset_deadline", reset_deadline_);
    for (const Mapping& m : peripherals_) writer.Write("stm32f2_ticked", m.ticked);
    for (const Mapping& m : peripherals_) m.peripheral->SaveState(writer);
}

void Mcu::RestoreState(StateReader& reader, bool sram) {
    core_.RestoreState(reader);
    if (sram) {
        reader.ReadBytes("stm32f2_sram", sram_.data(), sram_.size());
        sram_dirty_.fill(~uint64_t{0});
    }
    reader.Read("stm32f2_reset_deadline", reset_deadline_);
    for (Mapping& m : peripherals_) reader.Read("stm32f2_ticked", m.ticked);
    for (const Mapping& m : peripherals_) m.peripheral->RestoreState(reader);
    RecomputeNextEvent();
}

void Mcu::CopyDirtySram(uint8_t* to, const uint8_t* from) const {
    constexpr uint32_t kPage = 1u << cortex_m3::Core::kDirtyPageShift;
    for (uint32_t page = 0u; page < kSramBytes / kPage; ++page) {
        if ((sram_dirty_[page >> 6] >> (page & 63u)) & 1u)
            std::memcpy(to + page * kPage, from + page * kPage, kPage);
    }
}

void Mcu::SnapshotMemory(MemorySnapshot& snapshot) {
    if (!snapshot.synced) {
        snapshot.synced = true;
        snapshot.sram = sram_;
        snapshot.flash_generation = flash_generation_ - 1u;
    } else {
        CopyDirtySram(snapshot.sram.data(), sram_.data());
    }
    sram_dirty_.fill(0u);
    if (snapshot.flash_generation != flash_generation_) {
        snapshot.flash = flash_;
        snapshot.otp = otp_;
        snapshot.flash_generation = flash_generation_;
    }
}

void Mcu::RestoreMemory(MemorySnapshot& snapshot) {
    if (!snapshot.synced) {
        sram_ = snapshot.sram;
        snapshot.synced = true;
        flash_generation_ = snapshot.flash_generation + 1u;
    } else {
        CopyDirtySram(sram_.data(), snapshot.sram.data());
    }
    sram_dirty_.fill(0u);
    if (flash_generation_ != snapshot.flash_generation) {
        std::copy(snapshot.flash.begin(), snapshot.flash.end(), flash_.begin());
        std::copy(snapshot.otp.begin(), snapshot.otp.end(), otp_.begin());
        core_.InvalidateCode(kFlashBase, kFlashBytes);
        flash_generation_ = snapshot.flash_generation;
    }
}

}

}
