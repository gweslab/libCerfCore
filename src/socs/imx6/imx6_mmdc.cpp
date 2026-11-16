#include <cerfcore/boards/board_context.h>
#include "../../boards/page_table_builder.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_mmio_lane.h"

#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace {

constexpr uint32_t kMmdcBase = 0x021B0000u;
constexpr uint32_t kMmdcSize = 0x00004000u;

constexpr uint32_t kOffMdasp = 0x40u;
constexpr uint32_t kOffMaarcr = 0x400u;
constexpr uint32_t kMaarcrReset = 0x514201F0u;

constexpr uint32_t kCsDecodeUnit = 0x02000000u;

class Imx6Mmdc final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return kMmdcBase; }
    uint32_t MmioSize() const override { return kMmdcSize; }

    uint8_t ReadByte(uint32_t a) override {
        return Imx6ReadMmioByte(a, [this](uint32_t address) { return ReadWord(address); });
    }
    uint16_t ReadHalf(uint32_t a) override {
        return Imx6ReadMmioHalf(a, [this](uint32_t address) { return ReadWord(address); });
    }
    uint32_t ReadWord(uint32_t a) override {
        const uint32_t off = a - kMmdcBase;
        if (off == kOffMdasp) return Cs0End();
        HaltUnsupportedAccess("imx6-mmdc read32 unmodelled register", a, 0);
    }

    void WriteByte(uint32_t a, uint8_t v) override {
        HaltUnsupportedAccess("imx6-mmdc write8 unmodelled register", a, v);
    }
    void WriteHalf(uint32_t a, uint16_t v) override {
        HaltUnsupportedAccess("imx6-mmdc write16 unmodelled register", a, v);
    }
    void WriteWord(uint32_t a, uint32_t v) override {
        const uint32_t off = a - kMmdcBase;
        if (off == kOffMaarcr && v == kMaarcrReset) return;
        HaltUnsupportedAccess("imx6-mmdc write32 unmodelled register", a, v);
    }

private:
    uint32_t Cs0SizeBytes() const {
        const PageTableBuilder& builder = emu_.Get<PageTableBuilder>();
        const uint64_t bytes = builder.DramChipSelectBytes();
        if (bytes == 0u || bytes % kCsDecodeUnit != 0u)
            emu_.Get<Fatal>().Die("i.MX6 MMDC: populated DDR 0x%llX is not a whole number of CS0_END units",
                                  static_cast<unsigned long long>(bytes));
        return static_cast<uint32_t>(bytes);
    }

    uint32_t Cs0End() const {
        return (Cs0SizeBytes() / kCsDecodeUnit + 0x7u) & 0x7Fu;
    }
};

}

REGISTER_SERVICE(Imx6Mmdc);

}
