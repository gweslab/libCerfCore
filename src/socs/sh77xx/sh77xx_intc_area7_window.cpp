#include "sh77xx_intc.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

class Sh77xxIntcArea7Window : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.TryGet<Sh77xxIntc>() != nullptr; }

    void OnReady() override {
        intc_ = &emu_.Get<Sh77xxIntc>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return 0x1FFFFEE0u; }
    uint32_t MmioSize() const override { return 6u; }

    uint16_t ReadHalf(uint32_t addr) override { return intc_->ReadArea7Half(addr); }
    void     WriteHalf(uint32_t addr, uint16_t value) override { intc_->WriteArea7Half(addr, value); }

private:
    Sh77xxIntc* intc_ = nullptr;
};

}

REGISTER_SERVICE(Sh77xxIntcArea7Window);

}
