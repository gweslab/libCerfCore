#pragma once

#include "vr41xx_aiu_dma.h"
#include "vr41xx_dmaau.h"
#include "../reg_window_impl.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

template <const std::string_view& Soc, const cerf_reg_window_detail::RegWindowModel& M>
class Vr41xxDmaauImpl
    : public cerf_reg_window_detail::RegWindowBase<Soc, M, Vr41xxDmaau> {
    using Window = cerf_reg_window_detail::RegWindowBase<Soc, M, Vr41xxDmaau>;

public:
    using Window::Window;

    uint16_t ReadHalf(uint32_t addr) override {
        this->emu_.template Get<Vr41xxAiuDma>().SyncDma();
        return Window::ReadHalf(addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        this->emu_.template Get<Vr41xxAiuDma>().WriteDmaRegister([&] { Window::WriteHalf(addr, value); });
    }

    uint32_t Base(Vr41xxAiuChannel channel) const override { return Pair(BaseIndex(channel)); }

    uint32_t Address(Vr41xxAiuChannel channel) const override { return Pair(BaseIndex(channel) + 2u); }

    void SetAddress(Vr41xxAiuChannel channel, uint32_t pa) override {
        const uint32_t lo = BaseIndex(channel) + 2u;
        this->ApplyStoredWrite(lo * 2u, static_cast<uint16_t>(pa & M.reg[lo].wmask));
        this->ApplyStoredWrite((lo + 1u) * 2u, static_cast<uint16_t>((pa >> 16) & M.reg[lo + 1u].wmask));
    }

private:
    static constexpr uint32_t BaseIndex(Vr41xxAiuChannel channel) {
        return channel == Vr41xxAiuChannel::In ? 0u : 4u;
    }

    uint32_t Pair(uint32_t lo) const {
        return this->StoredReg(lo) | (static_cast<uint32_t>(this->StoredReg(lo + 1u)) << 16);
    }
};

}
