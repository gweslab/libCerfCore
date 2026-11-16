#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_mmio_lane.h"
#include "imx6_id.h"
#include "imx6_variant.h"

namespace cerfcore {

namespace {
constexpr uint32_t kPl310TagRamReset = 0u;
constexpr uint32_t kPl310DataRamReset = 0u;

class Imx6Pl310 final : public Peripheral {
public:
    using Peripheral::Peripheral;
    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            Reset();
        });
    }
    uint32_t MmioBase() const override { return 0x00A02000u; }
    uint32_t MmioSize() const override { return 0x1000u; }
    uint8_t ReadByte(uint32_t address) override {
        return Imx6ReadMmioByte(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t address) override {
        return Imx6ReadMmioHalf(address, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t address) override {
        const uint32_t offset = address - MmioBase();
        const Imx6Variant& variant = emu_.Get<Imx6Variant>();
        if (offset == 0u) return variant.Pl310CacheId();
        if (offset == 4u) {
            constexpr uint32_t kSizeMask = (7u << 20) | (7u << 8);
            constexpr uint32_t kAssociativityMask = (1u << 18) | (1u << 6);
            const uint32_t auxiliary = regs_[0x104u >> 2];
            const uint32_t way_size = (auxiliary >> 17) & 7u;
            const uint32_t associativity = (auxiliary >> 16) & 1u;
            const uint32_t base = variant.Pl310CacheType();
            return (base & ~(kSizeMask | kAssociativityMask)) | (way_size << 20) | (way_size << 8) |
                   (associativity << 18) | (associativity << 6);
        }
        if (IsReadRegister(offset)) return regs_[offset >> 2];
        HaltUnsupportedAccess("imx6-pl310 read32 unmodelled register", address, 0);
    }
    void WriteByte(uint32_t address, uint8_t value) override {
        Imx6ForEachMmioLane(address, value, Imx6MmioWidth::Byte,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t address, uint16_t value) override {
        Imx6ForEachMmioLane(address, value, Imx6MmioWidth::Half,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t address, uint32_t value) override {
        const uint32_t offset = address - MmioBase();
        if (!IsWriteRegister(offset)) HaltUnsupportedAccess("imx6-pl310 write32 unmodelled register", address, value);
        if (offset == 0x100u) value &= 1u;
        if (IsMaintenanceOperation(offset)) {
            regs_[offset >> 2] = 0u;
        } else if (IsReadRegister(offset)) {
            regs_[offset >> 2] = value;
        }
    }
    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_, sizeof(regs_)); }
    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }

private:
    static bool IsMaintenanceOperation(uint32_t offset) {
        switch (offset) {
        case 0x730u:
        case 0x770u:
        case 0x77Cu:
        case 0x7B0u:
        case 0x7BCu:
        case 0x7F0u:
        case 0x7FCu: return true;
        default: return false;
        }
    }

    static bool IsReadRegister(uint32_t offset) {
        switch (offset) {
        case 0x100u:
        case 0x104u:
        case 0x108u:
        case 0x10Cu:
        case 0xF60u: return true;
        default: return IsMaintenanceOperation(offset) && offset != 0x770u;
        }
    }

    static bool IsWriteRegister(uint32_t offset) {
        switch (offset) {
        case 0x108u:
        case 0x10Cu:
        case 0x220u: return true;
        default: return IsReadRegister(offset) || offset == 0x770u;
        }
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t offset = lane.address - MmioBase();
        if (!IsWriteRegister(offset))
            HaltUnsupportedAccess("imx6-pl310 write lane unmodelled register",
                                  lane.address, lane.value);
        if (IsMaintenanceOperation(offset)) {
            regs_[offset >> 2] = 0u;
        } else {
            WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
        }
    }
    uint32_t regs_[0x1000u / 4u]{};

    void Reset() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        regs_[0x104u >> 2] = emu_.Get<Imx6Variant>().Pl310AuxiliaryReset();
        regs_[0x108u >> 2] = kPl310TagRamReset;
        regs_[0x10Cu >> 2] = kPl310DataRamReset;
    }
};
REGISTER_SERVICE(Imx6Pl310);
}

}
