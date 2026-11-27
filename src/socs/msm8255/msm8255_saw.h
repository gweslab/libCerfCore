#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <atomic>
#include <cstdint>

namespace cerfengine {
class ArmJit;
}

namespace cerfcore {

class Msm8255Saw : public Peripheral {
public:
    static constexpr uint32_t kRegFileWords = 11u;

    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;

    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    bool TakeCoreCollapse();
    void CollapseCore();
    void RequestPowerUp(bool requested);

private:
    static bool     InRegFile(uint32_t off);
    static uint32_t RegIndex(uint32_t off);

    void OnIdle();
    void Reset();

    cerfengine::ArmJit*   jit_ = nullptr;
    std::atomic<uint32_t> reg04_{0};
    std::atomic<uint32_t> vctl_{0};
    std::atomic<uint32_t> regs_[kRegFileWords] = {};
    bool                  core_collapsed_      = false;
    bool                  power_up_requested_  = false;
};

}
