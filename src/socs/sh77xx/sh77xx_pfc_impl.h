#pragma once

#include "../reg_window_impl.h"
#include "sh77xx_pfc.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

namespace sh77xx_pfc {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kControlBase = 0x04000100u;

constexpr RegWindowModel ControlWindow(const uint16_t (&reset)[kSh77xxPortCount]) {
    RegWindowModel w{kControlBase, kSh77xxPortCount * 2u, kSh77xxPortCount, false, {}, 2u};
    for (uint32_t i = 0; i < kSh77xxPortCount; ++i) {
        w.reg[i] = RegSpec{ReadKind::kStored, WriteKind::kStored, 0xFFFFu, reset[i], 0u, OtherReset::kRetain};
    }
    return w;
}

template <const std::string_view& Soc, const RegWindowModel& M>
class Sh77xxPfcBase : public RegWindowBase<Soc, M, Sh77xxPfc> {
    using Base = RegWindowBase<Soc, M, Sh77xxPfc>;

public:
    using Base::Base;

protected:
    uint16_t PortControl(uint32_t port) const override { return this->StoredReg(port); }

    void AfterStore(uint32_t i) override {
        (void)i;
        this->NotifyControlChanged();
    }
};

}

}
