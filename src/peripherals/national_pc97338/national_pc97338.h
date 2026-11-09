#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>
#include <functional>

namespace cerfcore {

class NationalPc97338 : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return isa_io_base_ + kIndexPort; }
    uint32_t MmioSize() const override { return 2u; }

    uint8_t ReadByte(uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    uint32_t IsaIoBase() const { return isa_io_base_; }
    bool     ParallelPortEnabled() const { return (fer_ & kFerParallelPort) != 0u; }
    bool     Scc1Enabled() const { return (fer_ & kFerScc1) != 0u; }
    bool     ExtendedParallelPort() const { return (ptr_ & kPtrExtendedParallel) != 0u; }
    bool     ClockRunning() const;

    void RegisterParallelIrqEnableProbe(std::function<bool()> probe) { parallel_irq_enabled_ = std::move(probe); }

private:
    static constexpr uint32_t kIndexPort = 0x398u;

    static constexpr uint8_t kFerParallelPort    = 0x01u;
    static constexpr uint8_t kFerScc1            = 0x02u;
    static constexpr uint8_t kFerUnmodeled       = 0x3Cu;
    static constexpr uint8_t kFerDefined         = 0x3Fu;
    static constexpr uint8_t kPtrPowerDown       = 0x01u;
    static constexpr uint8_t kPtrMustBeZero      = 0x04u;
    static constexpr uint8_t kPtrLpt2Irq7        = 0x08u;
    static constexpr uint8_t kPtrScc1TestMode    = 0x10u;
    static constexpr uint8_t kPtrLock            = 0x40u;
    static constexpr uint8_t kPtrExtendedParallel = 0x80u;
    static constexpr uint8_t kPtrUnmodeled = kPtrPowerDown | kPtrMustBeZero | kPtrLpt2Irq7 | kPtrScc1TestMode;
    static constexpr uint8_t kPtrStored    = kPtrLock | kPtrExtendedParallel;
    static constexpr uint8_t kScf2Vld            = 0x03u;
    static constexpr uint8_t kScf2Scc1BankSelect = 0x10u;
    static constexpr uint8_t kScf2Scc2NormalPower = 0x20u;
    static constexpr uint8_t kScf2Scc2BankSelect = 0x80u;
    static constexpr uint8_t kClkSource          = 0x03u;
    static constexpr uint8_t kClkMultiplier      = 0x04u;
    static constexpr uint8_t kClkValid           = 0x08u;
    static constexpr uint8_t kClkSourceMultiplier14 = 0x00u;
    static constexpr uint8_t kClkSourceMultiplier24 = 0x01u;
    static constexpr uint8_t kClkSourceReserved  = 0x03u;

    static constexpr uint8_t kResetFer  = 0x00u;
    static constexpr uint8_t kResetFar  = 0x10u;
    static constexpr uint8_t kResetPtr  = 0x00u;
    static constexpr uint8_t kResetScf2 = 0x20u;
    static constexpr uint8_t kSid       = 0xB0u;
    static constexpr uint8_t kIndexId   = 0x88u;
    static constexpr uint8_t kIndexReservedReadZero = 0x70u;
    static constexpr uint8_t kIndexReadable         = 0x0Fu;

    void    OnReset(ResetLineKind kind);
    uint8_t ReadIndex(uint32_t addr);
    uint8_t ReadConfig(uint8_t index);
    void    WriteConfig(uint8_t index, uint8_t value);
    void    WriteClk(uint8_t value);

    uint32_t isa_io_base_ = 0u;
    uint8_t  index_ = 0u;
    bool     index_id_pending_ = true;
    bool     index_written_    = false;
    uint8_t  fer_   = kResetFer;
    uint8_t  far_   = kResetFar;
    uint8_t  ptr_   = kResetPtr;
    uint8_t  scf2_  = kResetScf2;
    uint8_t  clk_   = 0u;
    bool     clk_source_written_ = false;

    std::function<bool()> parallel_irq_enabled_;
};

}
