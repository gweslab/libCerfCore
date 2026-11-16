#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_mmio_lane.h"

#include <array>
#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace {

struct Cs0Field { uint32_t mask; const char* name; };
constexpr Cs0Field kCs0Fields[] = {
    {0xF0000000u, "CS0GCR1 PSZ changes the modeled CS0 decode"},
    {1u << 27, "CS0GCR1 WP changes the modeled CS0 decode"},
    {7u << 24, "CS0GCR1 GBC changes the modeled CS0 decode"},
    {1u << 23, "CS0GCR1 AUS changes the modeled CS0 decode"},
    {7u << 20, "CS0GCR1 CSREC changes the modeled CS0 decode"},
    {1u << 19, "CS0GCR1 SP changes the modeled CS0 decode"},
    {7u << 16, "CS0GCR1 DSZ changes the modeled CS0 decode"},
    {3u << 14, "CS0GCR1 BCS changes the modeled CS0 decode"},
    {3u << 12, "CS0GCR1 BCD changes the modeled CS0 decode"},
    {1u << 11, "CS0GCR1 WC changes the modeled CS0 decode"},
    {7u << 8, "CS0GCR1 BL changes the modeled CS0 decode"},
    {1u << 7, "CS0GCR1 CREP changes the modeled CS0 decode"},
    {1u << 6, "CS0GCR1 CRE changes the modeled CS0 decode"},
    {1u << 5, "CS0GCR1 RFL changes the modeled CS0 decode"},
    {1u << 4, "CS0GCR1 WFL changes the modeled CS0 decode"},
    {1u << 3, "CS0GCR1 MUM changes the modeled CS0 decode"},
    {1u << 2, "CS0GCR1 SRD changes the modeled CS0 decode"},
    {1u << 1, "CS0GCR1 SWR changes the modeled CS0 decode"},
    {1u, "CS0GCR1 CSEN changes the modeled CS0 decode"}
};
constexpr std::array<uint32_t, 12> kEimReset = {
    0x00610088u, 0x00001010u, 0x1C002000u, 0x00000000u,
    0x00000000u, 0x00000000u, 0x00000000u, 0x00001010u,
    0x1C002000u, 0x00000000u, 0x00000000u, 0x00000000u
};
constexpr std::array<uint32_t, 12> kEimGuestValue = {
    0x01120001u, 0x00000000u, 0x04000011u, 0x00000000u,
    0x02000249u, 0x00000000u, 0x11120201u, 0x00000000u,
    0x0B000301u, 0x00000000u, 0x0B0000C1u, 0x00000000u
};
constexpr std::array<uint32_t, 12> kEimGuestAlternateValue = {
    0x01120001u, 0x00001010u, 0x01000001u, 0x00000000u,
    0x02000000u, 0x00000000u, 0x11120201u, 0x00001010u,
    0x1C002000u, 0x00000000u, 0x1C002000u, 0x00000000u
};
constexpr std::array<const char*, 12> kEimRegisterNames = {
    "CS0GCR1", "CS0GCR2", "CS0RCR1", "CS0RCR2", "CS0WCR1", "CS0WCR2",
    "CS1GCR1", "CS1GCR2", "CS1RCR1", "CS1RCR2", "CS1WCR1", "CS1WCR2"
};

class Imx6Weim : public Peripheral {
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
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
    }

    void Reset() { registers_ = kEimReset; }

    uint32_t MmioBase() const override { return 0x021B8000u; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off <= 0x2Cu && (off & 3u) == 0u) return registers_[off >> 2];
        if (off == 0x98u) return 0u;
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) {
            WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
        });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) {
            WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
        });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (IsRegister(off)) {
            if (off >= 0x30u && off <= 0x5Cu)
                HaltUnsupportedAccess("EIM CS2/CS3 register has no guest writer", addr, value);
            if (off <= 0x2Cu) {
                const uint32_t old = registers_[off >> 2];
                const uint32_t index = off >> 2;
                const uint32_t reset = kEimReset[index];
                const uint32_t guest = kEimGuestValue[index];
                const uint32_t alternate = kEimGuestAlternateValue[index];
                if (value != old && value != reset && value != guest && value != alternate) {
                    if (off == 0x00u) {
                        for (const auto& field : kCs0Fields) {
                            if ((value & field.mask) != (reset & field.mask) &&
                                (value & field.mask) != (guest & field.mask))
                                HaltUnsupportedAccess(field.name, addr, value);
                        }
                    }
                    HaltUnsupportedAccess(kEimRegisterNames[index], addr, value);
                }
                registers_[index] = value;
            } else if (off == 0x90u) {
                const struct { uint32_t mask; const char* name; } fields[] = {
                    {0xFFFFF800u, "EIM_WCR reserved [31:11] field is read-only"},
                    {3u << 9, "EIM_WCR WDOG_LIMIT field has no guest writer"},
                    {1u << 8, "EIM_WCR WDOG_EN field has no guest writer"},
                    {3u << 6, "EIM_WCR reserved [7:6] field is read-only"},
                    {1u << 5, "EIM_WCR INTPOL field has no guest writer"},
                    {1u << 4, "EIM_WCR INTEN field has no guest writer"},
                    {1u << 3, "EIM_WCR reserved bit 3 is read-only"},
                    {3u << 1, "EIM_WCR GBCD field has no guest writer"},
                    {1u, "EIM_WCR BCM field has no guest writer"}
                };
                for (const auto& field : fields) {
                    if ((value & field.mask) != 0u) HaltUnsupportedAccess(field.name, addr, value);
                }
            } else if (off == 0x94u) {
                HaltUnsupportedAccess("EIM_WIAR write has no guest writer", addr, value);
            } else if (off == 0x98u) {
                HaltUnsupportedAccess("EIM_EAR is read-only", addr, value);
            }
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("registers", registers_.data(), sizeof(registers_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes("registers", registers_.data(), sizeof(registers_)); }

private:
    static bool IsRegister(uint32_t off) {
        return ((off <= 0x5Cu) || (off >= 0x90u && off <= 0x98u)) &&
               (off & 3u) == 0u;
    }

    std::array<uint32_t, 12> registers_{};
};
}

REGISTER_SERVICE(Imx6Weim);

}
