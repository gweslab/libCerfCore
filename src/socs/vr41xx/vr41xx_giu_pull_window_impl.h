#pragma once

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "vr41xx_giu.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

namespace cerf_vr41xx_giu_detail {

constexpr uint32_t kPullWindowBase = 0x0B0002E0u;
constexpr uint32_t kPullWindowSize = 0x20u;

template <const std::string_view& Soc>
class Vr41xxGiuPullWindowBase : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == Soc; }

    void OnReady() override {
        giu_ = &emu_.Get<Vr41xxGiu>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kPullWindowBase; }
    uint32_t MmioSize() const override { return kPullWindowSize; }

    uint16_t ReadHalf(uint32_t addr) override { return giu_->ReadPullRegister(addr - kPullWindowBase); }
    void     WriteHalf(uint32_t addr, uint16_t value) override {
        giu_->WritePullRegister(addr - kPullWindowBase, value);
    }

private:
    Vr41xxGiu* giu_ = nullptr;
};

}

}
