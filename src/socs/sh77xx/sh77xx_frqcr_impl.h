#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../reg_window_impl.h"
#include "sh77xx_cpg_impl.h"
#include "sh77xx_frqcr.h"
#include <cerfcore/core/callback_list.h>

#include <cstdint>
#include <string_view>
#include <utility>

namespace cerfcore {

namespace sh77xx_frqcr {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kFrqcrPa = 0x1FFFFF80u;
inline constexpr uint16_t kCkoen   = 1u << 8;

constexpr RegWindowModel Window(uint16_t writable, uint16_t write_zero) {
    RegWindowModel w{kFrqcrPa, 2u, 1u, false, {}, 2u};
    w.reg[0] = RegSpec{ReadKind::kStored, WriteKind::kStored, writable, kSh77xxFrqcrReset, write_zero,
                       OtherReset::kRetain};
    return w;
}

template <const std::string_view& Soc, const RegWindowModel& W>
class Sh77xxFrqcrBase : public RegWindowBase<Soc, W, Sh77xxFrqcr> {
    using Base = RegWindowBase<Soc, W, Sh77xxFrqcr>;

public:
    using Base::Base;

    void OnReady() override {
        Base::OnReady();
        applied_ = this->StoredReg(0);
        this->emu_.template Get<GuestCpuReset>().RegisterResetReleaseListener([this] { Apply(this->StoredReg(0)); });
    }

    void PostRestore() override {
        applied_ = this->StoredReg(0);
        SetCpuRate(applied_);
    }

    uint16_t Value() const override { return this->StoredReg(0); }

    void RegisterChangeListener(Sh77xxFrqcr::ChangeListener listener) override {
        listeners_.Add(std::move(listener));
    }

protected:
    void BeforeStore(uint32_t, uint16_t value) override {
        if ((value & kCkoen) == 0u) {
            this->emu_.template Get<Fatal>().Die("Sh77xxFrqcr: FRQCR write 0x%04X clears CKOEN", value);
        }
        Apply(value);
    }

private:
    void SetCpuRate(uint16_t frqcr) {
        this->emu_.template Get<GuestCycleClock>().SetClockRate(
            this->emu_.template Get<Sh77xxCpg>().CpuRate(frqcr));
    }

    void Apply(uint16_t frqcr) {
        const uint16_t from = applied_;
        applied_            = frqcr;
        SetCpuRate(frqcr);
        listeners_.Call(from, frqcr);
    }

    CallbackList<uint16_t, uint16_t>         listeners_;
    uint16_t                                 applied_ = kSh77xxFrqcrReset;
};

}

}
