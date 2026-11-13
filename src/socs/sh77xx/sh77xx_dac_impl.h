#pragma once

#include <cstdint>
#include <string_view>

#include "../reg_window_impl.h"
#include "sh77xx_dac.h"

namespace cerfcore {

namespace sh77xx_dac {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kBase  = 0x040000A0u;
inline constexpr uint32_t kDadr0 = 0u;
inline constexpr uint32_t kDadr1 = 2u;
inline constexpr uint32_t kDacr  = 4u;

inline constexpr uint16_t kDaoe1 = 0x80u;
inline constexpr uint16_t kDaoe0 = 0x40u;

constexpr RegWindowModel WindowOf(OtherReset other_reset) {
    RegWindowModel w{kBase, 5u, 5u, false, {}, 1u};
    w.reg[kDadr0] = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xFFu, 0x00u, 0x00u, other_reset};
    w.reg[kDadr1] = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xFFu, 0x00u, 0x00u, other_reset};
    w.reg[kDacr]  = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xE0u, 0x1Fu, 0x00u, other_reset};
    return w;
}

template <const std::string_view& Soc, const RegWindowModel& W>
class Sh77xxDacBase : public cerf_reg_window_detail::RegWindowBase<Soc, W, Sh77xxDac> {
public:
    using cerf_reg_window_detail::RegWindowBase<Soc, W, Sh77xxDac>::RegWindowBase;

    Sh77xxDac::Output ChannelOutput(uint32_t channel) const override {
        const uint16_t enable = channel == 0u ? kDaoe0 : kDaoe1;
        return Sh77xxDac::Output{(this->StoredReg(kDacr) & enable) != 0u,
                                 static_cast<uint8_t>(this->StoredReg(channel == 0u ? kDadr0 : kDadr1))};
    }

    void RestoreState(StateReader& r) override {
        cerf_reg_window_detail::RegWindowBase<Soc, W, Sh77xxDac>::RestoreState(r);
        this->NotifyRestored();
    }

protected:
    void AfterStore(uint32_t i) override {
        (void)i;
        this->NotifyOutputChanged();
    }

    void AfterReset() override { this->NotifyOutputChanged(); }
};

}

}
