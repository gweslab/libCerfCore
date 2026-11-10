#pragma once

#include <cstdint>
#include <mutex>

#include "../../socs/reg_window_impl.h"

namespace cerfcore {

class Hd64461IrqOutput;

enum class Hd64461IntcSource : uint8_t {
    kUart = 5,
    kIrda = 6,
    kTmu1 = 9,
    kTmu0 = 10,
    kGpio = 11,
    kAfe  = 12,
    kPcc1 = 13,
    kPcc0 = 14,
};

inline constexpr cerf_reg_window_detail::RegWindowModel kHd64461IntcModel = {
    0x10005000u,
    4u,
    2u,
    false,
    {
        {cerf_reg_window_detail::ReadKind::kSampled, cerf_reg_window_detail::WriteKind::kFatal, 0x0000u, 0x0000u,
         0x0000u, cerf_reg_window_detail::OtherReset::kRetain, 0x0000u},
        {cerf_reg_window_detail::ReadKind::kStored, cerf_reg_window_detail::WriteKind::kStored, 0xFFFFu, 0x0000u,
         0x0000u, cerf_reg_window_detail::OtherReset::kRetain, 0x0000u},
    },
};

class Hd64461Intc : public cerf_reg_window_detail::RegWindow<kHd64461IntcModel, Peripheral, std::mutex> {
public:
    using RegWindow::RegWindow;

    bool ShouldRegister() override;
    void OnReady() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void SetSourceRequest(Hd64461IntcSource source, bool requesting);

protected:
    uint16_t SampleRegister(uint32_t i, uint32_t addr) override;
    void     AfterStore(uint32_t i) override;

private:
    void DriveLocked();

    Hd64461IrqOutput* output_   = nullptr;
    uint16_t          requests_ = 0u;
};

}
