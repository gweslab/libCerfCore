#pragma once

#include "../irq_controller.h"

#include <cstdint>
#include <mutex>

namespace cerfengine { class ArmJit; }

namespace cerfcore {

class Msm8255DemMaster;
class StateReader;
class StateWriter;

class Msm8255Vic : public IrqController {
public:
    static constexpr uint32_t kSourceCount = 128;
    static constexpr uint32_t kBankCount   = 4;
    static constexpr uint32_t kBitsPerBank = 32;

    struct SourceLine {
        uint32_t bank;
        uint32_t mask;
    };

    struct SourceSet {
        uint32_t banks[kBankCount]{};

        void Add(uint32_t source);
        bool Has(uint32_t source) const;
    };

    static SourceLine LineOf(uint32_t source);
    static uint32_t   LowestSource(uint32_t bank, uint32_t bits);

    using IrqController::IrqController;

    bool ShouldRegister() override;
    void OnReady() override;

    void AssertIrq   (int source_bit)            override;
    void AssertSubIrq(int main_bit, int sub_bit) override;
    void DeAssertIrq (int source_bit)            override;
    void PulseIrq    (int source_bit)            override;
    uint32_t ReadPendingVector()                 override;

    void MonitorWakeSources(const SourceSet& sources);

    uint32_t ReadReg (uint32_t off);
    void     WriteReg(uint32_t off, uint32_t value);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    SourceLine CheckedLine(int source_bit, const char* op) const;
    void       RequireActiveHigh(const SourceLine& line, int source_bit) const;
    uint32_t PendingBank(uint32_t bank) const;
    uint32_t IrqBank(uint32_t bank) const;
    uint32_t FiqBank(uint32_t bank) const;
    bool     HasPendingIrq() const;
    bool     HasPendingFiq() const;
    void     Republish();

    static int BankInSet(uint32_t off, uint32_t base);

    mutable std::mutex   state_mutex_;
    cerfengine::ArmJit*  jit_ = nullptr;
    Msm8255DemMaster*    dem_ = nullptr;
    uint32_t raw_[kBankCount]{};
    uint32_t latch_[kBankCount]{};
    uint32_t enable_[kBankCount]{};
    uint32_t type_[kBankCount]{};
    uint32_t select_[kBankCount]{};
    uint32_t polarity_[kBankCount]{};
    uint32_t softint_[kBankCount]{};
    uint32_t vect_priority_[kSourceCount]{};
    uint32_t no_pend_val_ = 0;
    uint32_t master_en_   = 0;
    uint32_t config_      = 0;
    SourceSet wake_monitor_{};
};

}
