#pragma once

#include "../../cpu/cortex_m3/cortex_m3_core.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

namespace stm32f2 {

class Peripheral {
public:
    virtual ~Peripheral() = default;
    virtual uint32_t Read(uint32_t offset, uint32_t size) = 0;
    virtual void Write(uint32_t offset, uint32_t size, uint32_t value) = 0;
    virtual void SaveState(StateWriter& writer) const = 0;
    virtual void RestoreState(StateReader& reader) = 0;

    virtual void Tick(uint32_t hclk_cycles) { (void)hclk_cycles; }
    virtual uint64_t CyclesToEvent() const { return UINT64_MAX; }
    virtual void ClockChanged() {}
    virtual bool Peek(uint32_t offset, uint32_t* value) const {
        (void)offset;
        (void)value;
        return false;
    }
};

class FlashProgrammer {
public:
    virtual ~FlashProgrammer() = default;
    virtual void Program(uint32_t address, uint32_t size, uint32_t value) = 0;
};

class McuHost {
public:
    virtual ~McuHost() = default;
    [[noreturn]] virtual void McuFault(const char* what, uint32_t detail, uint32_t pc) = 0;
};

class Mcu : public cortex_m3::Bus {
public:
    static constexpr uint32_t kFlashBase = 0x08000000u;
    static constexpr uint32_t kFlashBytes = 0x00040000u;
    static constexpr uint32_t kSramBase = 0x20000000u;
    static constexpr uint32_t kSramBytes = 0x00018000u;
    static constexpr uint32_t kInterruptLines = 81u;
    static constexpr uint32_t kOtpBase = 0x1FFF7800u;
    static constexpr uint32_t kOtpBytes = 0x210u;
    static constexpr uint32_t kPeripheralWindow = 0x400u;

    explicit Mcu(McuHost& host);

    std::vector<uint8_t>& Flash() { return flash_; }
    const std::vector<uint8_t>& Flash() const { return flash_; }
    std::vector<uint8_t>& Otp() { return otp_; }
    std::vector<uint8_t>& Sram() { return sram_; }

    void AddPeripheral(uint32_t base, Peripheral* peripheral);
    void GateClock(uint32_t base, std::function<bool()> enabled);
    uint64_t EventCycle(uint32_t base) const;
    void SetFlashProgrammer(FlashProgrammer* programmer) { flash_programmer_ = programmer; }
    uint8_t* ProgramTarget(uint32_t address, uint32_t size);
    void InvalidateCode(uint32_t address, uint32_t size) {
        core_.InvalidateCode(address, size);
        ++flash_generation_;
    }
    void Start(uint32_t vector_table);
    void RunUntil(uint64_t cycle);

    void RequestSystemReset(uint32_t reset_flags) {
        reset_flags_ |= reset_flags;
        core_.LowerRunLimit(0u);
    }

    void SetResetDeadline(uint64_t cycle) {
        reset_deadline_ = cycle;
        core_.LowerRunLimit(cycle);
    }
    uint32_t TakeSystemReset() {
        const uint32_t flags = reset_flags_;
        reset_flags_ = 0u;
        return flags;
    }
    uint64_t Now() const { return core_.Cycles(); }
    void FlushTicks();
    void ClockChanged();
    void SetIrqLevel(uint32_t irq, bool asserted) {
        core_.SetExternalLevel(irq, asserted);
    }
    uint32_t ProgramCounter() const { return core_.ProgramCounter(); }
    const cortex_m3::Core& Core() const { return core_; }

    uint32_t Read(uint32_t address, uint32_t size) override;
    uint64_t Synchronize() override;
    void SystemResetRequest() override { RequestSystemReset(1u << 28); }
    bool EnterDeepSleep() override;
    void ExitDeepSleep() override;
    void Write(uint32_t address, uint32_t size, uint32_t value) override;
    [[noreturn]] void Fault(const char* what, uint32_t detail) override;

    void SaveState(StateWriter& writer, bool sram) const;
    void RestoreState(StateReader& reader, bool sram);

    struct MemorySnapshot {
        bool synced = false;
        uint64_t flash_generation = 0u;
        std::vector<uint8_t> flash;
        std::vector<uint8_t> otp;
        std::vector<uint8_t> sram;
    };
    void SnapshotMemory(MemorySnapshot& snapshot);
    void RestoreMemory(MemorySnapshot& snapshot);

private:
    struct Mapping {
        uint32_t base;
        Peripheral* peripheral;
        uint64_t ticked = 0u;
        std::function<bool()> clock_enabled;
    };

    void CatchUp(Mapping& m);
    bool Unclocked(const Mapping& m) const;
    void RecomputeNextEvent();
    void CopyDirtySram(uint8_t* to, const uint8_t* from) const;

    uint8_t* Memory(uint32_t address, uint32_t size, bool write);
    Mapping* Find(uint32_t address, uint32_t* offset);

    McuHost& host_;
    cortex_m3::Core core_;
    std::vector<uint8_t> flash_;
    std::vector<uint8_t> sram_;
    std::vector<uint8_t> otp_;
    std::vector<Mapping> peripherals_;
    FlashProgrammer* flash_programmer_ = nullptr;
    uint32_t reset_flags_ = 0u;
    uint64_t EventOf(const Mapping& m) const;
    uint32_t UnclockedValue(const Mapping& m, uint32_t offset, uint32_t address);
    uint64_t reset_deadline_ = UINT64_MAX;
    uint64_t flash_generation_ = 0u;
    uint64_t next_event_ = UINT64_MAX;
    std::array<uint64_t, ((kSramBytes >> cortex_m3::Core::kDirtyPageShift) + 63u) / 64u> sram_dirty_{};

};

}

}
