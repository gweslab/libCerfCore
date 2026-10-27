#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>

namespace cerfcore {

class CerfVirtPointer : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override;
    uint32_t MmioSize() const override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void SetPointer(uint32_t nx, uint32_t ny, uint32_t buttons);
    void AddWheel(int delta);
    void ClearButtons();

private:
    struct PointerState {
        uint32_t x;
        uint32_t y;
        uint32_t buttons;
    };

    static constexpr size_t kHeldLimit = 256;

    void Bump();
    void Publish(const PointerState& state);
    void Hold(const PointerState& next);

    std::atomic<uint32_t>    x_{0};
    std::atomic<uint32_t>    y_{0};
    std::atomic<uint32_t>    buttons_{0};
    std::atomic<uint32_t>    wheel_{0};
    std::atomic<uint32_t>    seq_{0};
    std::mutex               mutex_;
    std::deque<PointerState> held_;
    bool                     change_unseen_ = false;
};

}
