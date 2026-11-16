#pragma once

#include <cstdint>
#include <mutex>

#include <cerfcore/core/service.h>

namespace cerfcore {

inline constexpr uint16_t kInitialPenXStub = 0x800u;
inline constexpr uint16_t kInitialPenYStub = 0x800u;

class Tsc2017HostState : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;

    struct Sample {
        bool down = false;
        uint16_t x = kInitialPenXStub;
        uint16_t y = kInitialPenYStub;
        uint16_t z1 = 0x000u;
        uint16_t z2 = 0x000u;
    };

    enum class PenIrqMode : uint8_t { Enabled = 0, Disabled = 1, ForcedLow = 2 };

    using IrqChangedCallback = void (*)(void*);

    void SetPen(bool down, uint16_t raw_x, uint16_t raw_y);
    Sample Get();
    void SetIrqChangedCallback(IrqChangedCallback cb, void* ctx);
    bool PenIrqLineHigh();
    void SetPenIrqMode(PenIrqMode mode, bool notify);
    void ClearHostInputForRestore();

private:
    static void SetPenUpState(Sample& state);
    void NotifyIrqChanged();

    std::mutex mutex_;
    Sample state_;
    PenIrqMode penirq_mode_ = PenIrqMode::Enabled;
    IrqChangedCallback irq_cb_ = nullptr;
    void* irq_ctx_ = nullptr;
};

}
