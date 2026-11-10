#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "siemens_simpad_cs3_sink.h"
#include "siemens_simpad_family_id.h"
#include "../../peripherals/peripheral_dispatcher.h"

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase        = 0x18000000u;
constexpr uint32_t kStatusVs1   = 0x04u;
constexpr uint32_t kStatusVs2   = 0x08u;

class SiemensSimpadCs3Status : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::SiemensSimpad;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x00000080u; }

    uint8_t  ReadByte(uint32_t addr) override { return static_cast<uint8_t>(Status(addr)); }
    uint16_t ReadHalf(uint32_t addr) override { return static_cast<uint16_t>(Status(addr)); }
    uint32_t ReadWord(uint32_t addr) override { return Status(addr); }

private:
    uint32_t Status(uint32_t addr) {
        if (addr != kBase) HaltUnsupportedAccess("Read", addr, 0);
        uint32_t v = 0xFFFFFFFFu;
        auto& sink = emu_.Get<SiemensSimpadCs3Sink>();
        if (sink.CardVs1Grounded()) v &= ~kStatusVs1;
        if (sink.CardVs2Grounded()) v &= ~kStatusVs2;
        return v;
    }
};

}

REGISTER_SERVICE(SiemensSimpadCs3Status);

}
