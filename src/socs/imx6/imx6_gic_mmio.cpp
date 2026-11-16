#include "imx6_gic.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6GicMmio final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* board = emu_.TryGet<BoardContext>();
        return board && SocId::IsImx6(board->GetSocId());
    }

    void OnReady() override { emu_.Get<PeripheralDispatcher>().Register(this); }

    uint32_t MmioBase() const override { return Imx6Gic::kMmioBase; }
    uint32_t MmioSize() const override { return Imx6Gic::kMmioSize; }

    uint8_t ReadByte(uint32_t address) override {
        const uint32_t word = Gic().ReadMmio((address & ~3u) - MmioBase());
        return static_cast<uint8_t>(word >> ((address & 3u) * 8u));
    }

    uint16_t ReadHalf(uint32_t address) override {
        const uint32_t word = Gic().ReadMmio((address & ~3u) - MmioBase());
        return static_cast<uint16_t>(word >> ((address & 2u) * 8u));
    }

    uint32_t ReadWord(uint32_t address) override { return Gic().ReadMmio(address - MmioBase()); }

    void WriteByte(uint32_t address, uint8_t value) override {
        HaltUnsupportedAccess("imx6-gic write8", address, value);
    }

    void WriteHalf(uint32_t address, uint16_t value) override {
        HaltUnsupportedAccess("imx6-gic write16", address, value);
    }

    void WriteWord(uint32_t address, uint32_t value) override { Gic().WriteMmio(address - MmioBase(), value); }

    void SaveState(StateWriter& writer) override { Gic().SaveGicState(writer); }

    void RestoreState(StateReader& reader) override { Gic().RestoreGicState(reader); }

    void PostRestore() override { Gic().PostRestoreGicState(); }

private:
    Imx6Gic& Gic() { return emu_.Get<Imx6Gic>(); }
};

}

REGISTER_SERVICE(Imx6GicMmio);

}
