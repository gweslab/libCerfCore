#pragma once
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_mmio_lane.h"
#include "imx6_id.h"

namespace cerfcore {

template <uint32_t kBase> class Imx6Aipstz : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
    }
    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }
    uint8_t ReadByte(uint32_t address) override {
        return Imx6ReadMmioByte(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t address) override {
        return Imx6ReadMmioHalf(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t address) override {
        HaltUnsupportedAccess("read32", address, 0);
    }
    void WriteByte(uint32_t address, uint8_t value) override {
        Imx6ForEachMmioLane(address, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) {
            WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
        });
    }
    void WriteHalf(uint32_t address, uint16_t value) override {
        Imx6ForEachMmioLane(address, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) {
            WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
        });
    }
    void WriteWord(uint32_t address, uint32_t value) override {
        const uint32_t offset = address - kBase;
        const uint32_t index = RegisterIndex(offset);
        if (index >= kRegisterCount) HaltUnsupportedAccess("write32", address, value);
        const uint32_t guest_written = index < 2u ? 0x77777777u : 0u;
        if (value != guest_written)
            HaltUnsupportedAccess(RegisterName(index), address, value);
    }

private:
    static constexpr uint32_t kRegisterCount = 7u;

    static uint32_t RegisterIndex(uint32_t offset) {
        if (offset == 0u) return 0u;
        if (offset == 4u) return 1u;
        if (offset >= 0x40u && offset <= 0x50u && (offset & 3u) == 0u)
            return 2u + (offset - 0x40u) / 4u;
        return kRegisterCount;
    }

    static const char* RegisterName(uint32_t index) {
        static constexpr const char* names[] = {"AIPSTZ MPR0", "AIPSTZ MPR1", "AIPSTZ OPACR0",
                                                "AIPSTZ OPACR1", "AIPSTZ OPACR2", "AIPSTZ OPACR3",
                                                "AIPSTZ OPACR4"};
        return names[index];
    }

};

}
