#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "sh77xx_bsc.h"

#include <cstdint>

namespace cerfcore {

namespace {

class Sh77xxSdramModeRegister : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.TryGet<Sh77xxBsc>() != nullptr; }

    void OnReady() override {
        bsc_ = &emu_.Get<Sh77xxBsc>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kArea2Window; }
    uint32_t MmioSize() const override { return kWindowBytes * 2u; }

    void WriteByte(uint32_t addr, uint8_t value) override { SetMode(addr, value); }
    void WriteHalf(uint32_t addr, uint16_t value) override { SetMode(addr, value); }
    void WriteWord(uint32_t addr, uint32_t value) override { SetMode(addr, value); }

private:
    static constexpr uint32_t kArea2Window = 0x1FFFD000u;
    static constexpr uint32_t kWindowBytes = 0x1000u;

    void SetMode(uint32_t addr, uint32_t value) {
        const uint32_t area = addr < kArea2Window + kWindowBytes ? 2u : 3u;
        if (!bsc_->AreaIsSdram(area)) {
            HaltUnsupportedAccess("SDMR write for an area that BCR1 does not set to SDRAM", addr, value);
        }
    }

    Sh77xxBsc* bsc_ = nullptr;
};

}

REGISTER_SERVICE(Sh77xxSdramModeRegister);

}
