#include "sh77xx_bsc.h"
#include "sh77xx_pcmcia_card_bus.h"
#include "sh77xx_pfc.h"

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"

namespace cerfcore {

namespace {

constexpr uint32_t kArea5Base         = 0x14000000u;
constexpr uint32_t kAreas5And6Bytes   = 0x08000000u;
constexpr uint32_t kAreaShift         = 26u;
constexpr uint32_t kAreaOffsetMask    = 0x03FFFFFFu;
constexpr uint32_t kIoCardInterface   = 0x02000000u;
constexpr uint32_t kDynamicSizingArea = 6u;
constexpr uint32_t kPortG             = 6u;
constexpr uint32_t kPinIois16         = 7u;

class Sh77xxPcmciaSpace : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override { return emu_.TryGet<Sh77xxPcmciaCardBus>() != nullptr; }

    void OnReady() override {
        bsc_      = &emu_.Get<Sh77xxBsc>();
        card_bus_ = &emu_.Get<Sh77xxPcmciaCardBus>();
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kArea5Base; }
    uint32_t MmioSize() const override { return kAreas5And6Bytes; }

    uint8_t  ReadByte(uint32_t addr) override { return static_cast<uint8_t>(Read(addr, 1u)); }
    uint16_t ReadHalf(uint32_t addr) override { return static_cast<uint16_t>(Read(addr, 2u)); }
    uint32_t ReadWord(uint32_t addr) override { return Read(addr, 4u); }

    void WriteByte(uint32_t addr, uint8_t value) override { Write(addr, 1u, value); }
    void WriteHalf(uint32_t addr, uint16_t value) override { Write(addr, 2u, value); }
    void WriteWord(uint32_t addr, uint32_t value) override { Write(addr, 4u, value); }

private:
    struct Cycle {
        uint32_t      area;
        uint32_t      offset;
        bool          io;
        PcmciaIoWidth width;
    };

    Cycle Decode(uint32_t addr, uint32_t value) {
        Cycle c{addr >> kAreaShift, addr & kAreaOffsetMask, (addr & kIoCardInterface) != 0u,
                PcmciaIoWidth::kSixteenBit};
        if (!bsc_->AreaIsPcmcia(c.area)) HaltUnsupportedAccess("access with BCR1 AnPCM clear", addr, value);
        const uint32_t bus_bytes = bsc_->AreaBusBytes(c.area);
        if (bus_bytes == 4u) HaltUnsupportedAccess("PCMCIA access with BCR2 AnSZ longword", addr, value);
        if (bus_bytes == 1u) {
            c.width = PcmciaIoWidth::kEightBit;
        } else if (c.io && c.area == kDynamicSizingArea &&
                   emu_.Get<Sh77xxPfc>().PinMode(kPortG, kPinIois16) == kSh77xxPinModeOther) {
            c.width = PcmciaIoWidth::kCardIois16;
        }
        return c;
    }

    uint32_t Read(uint32_t addr, unsigned bytes) {
        const Cycle c = Decode(addr, 0u);
        return card_bus_->ReadCycles(c.area, c.offset, c.io, c.width, bytes);
    }

    void Write(uint32_t addr, unsigned bytes, uint32_t value) {
        const Cycle c = Decode(addr, value);
        card_bus_->WriteCycles(c.area, c.offset, c.io, c.width, bytes, value);
    }

    Sh77xxBsc*           bsc_      = nullptr;
    Sh77xxPcmciaCardBus* card_bus_ = nullptr;
};

}

REGISTER_SERVICE(Sh77xxPcmciaSpace);

}
