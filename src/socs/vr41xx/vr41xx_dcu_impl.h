#pragma once

#include "vr41xx_aiu_dma.h"
#include "vr41xx_dcu.h"
#include "../reg_window_impl.h"

#include <cerfcore/core/fatal.h>

#include <cstdint>
#include <string_view>

namespace cerfcore {

template <const std::string_view& Soc, const cerf_reg_window_detail::RegWindowModel& M>
class Vr41xxDcuImpl : public cerf_reg_window_detail::RegWindowBase<Soc, M, Vr41xxDcu> {
    using Window = cerf_reg_window_detail::RegWindowBase<Soc, M, Vr41xxDcu>;

public:
    using Window::Window;

    uint16_t ReadHalf(uint32_t addr) override {
        auto& aiu = this->emu_.template Get<Vr41xxAiuDma>();
        aiu.SyncDma();
        if (addr - M.base == kOffReq) return aiu.DmaRequests();
        return Window::ReadHalf(addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        auto& aiu = this->emu_.template Get<Vr41xxAiuDma>();
        aiu.WriteDmaRegister([&] {
            const uint32_t off = addr - M.base;
            if (off == kOffRst && (value & 1u) == 0u) {
                this->emu_.template Get<Fatal>().Die("Vr41xxDcu: DMARSTREG write 0x%04X resets the DMA controller",
                                                     value);
            }
            if (off == kOffMsk) {
                const uint16_t rising = static_cast<uint16_t>(value & ~this->StoredReg(kMskIndex));
                if (rising & aiu.DmaRequests()) {
                    this->emu_.template Get<Fatal>().Die(
                        "Vr41xxDcu: DMAMSKREG write 0x%04X enables a unit with a DMA request pending (requests 0x%04X)",
                        value, aiu.DmaRequests());
                }
            }
            Window::WriteHalf(addr, value);
        });
    }

    bool DmaEnabled(uint16_t msk_bit) const override {
        return (this->StoredReg(kSenIndex) & 1u) != 0u && (this->StoredReg(kMskIndex) & msk_bit) != 0u;
    }

private:
    static constexpr uint32_t kOffRst   = 0x00u;
    static constexpr uint32_t kOffMsk   = 0x06u;
    static constexpr uint32_t kOffReq   = 0x08u;
    static constexpr uint32_t kSenIndex = 2u;
    static constexpr uint32_t kMskIndex = 3u;
};

}
