#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "../../socs/reg_window_impl.h"

namespace cerfcore {

class Hd64461SocketWiring;

inline constexpr cerf_reg_window_detail::RegWindowModel kHd64461SystemControlModel = {
    0x10000000u,
    6u,
    3u,
    false,
    {
        {cerf_reg_window_detail::ReadKind::kStored, cerf_reg_window_detail::WriteKind::kStored, 0x3FFFu, 0x0000u,
         0xE000u, cerf_reg_window_detail::OtherReset::kRetain, 0x0000u, 0x0000u},
        {cerf_reg_window_detail::ReadKind::kSampled, cerf_reg_window_detail::WriteKind::kStored, 0x20F0u, 0x0000u,
         0xFF0Cu, cerf_reg_window_detail::OtherReset::kRetain, 0x0000u, 0x0000u},
        {},
    },
    2u,
    false,
    true,
};

class Hd64461SystemControl
    : public cerf_reg_window_detail::RegWindow<kHd64461SystemControlModel, Peripheral, std::mutex> {
public:
    using RegWindow::RegWindow;

    bool ShouldRegister() override;
    void OnReady() override;

    void RestoreState(StateReader& r) override;

    bool LcdcClockRunning() const;
    bool PccChannelClockRunning(uint32_t channel) const;
    bool TimerClockRunning(uint32_t channel) const;
    bool TimerOutputsSelected() const;

protected:
    uint16_t SampleRegister(uint32_t i, uint32_t addr) override;
    void     BeforeStore(uint32_t i, uint16_t value) override;
    void     AfterStore(uint32_t i) override;
    void     AfterReset() override;

private:
    void PublishLocked();

    const Hd64461SocketWiring* wiring_ = nullptr;
    std::atomic<uint16_t>      stbcr_{0u};
    std::atomic<uint16_t>      syscr_{0u};
};

}
