#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>

#include <cstdint>
#include "imx6_id.h"
#include "imx6_variant.h"

namespace cerfcore {

namespace {

constexpr uint32_t kSataBase = 0x02200000u;
constexpr uint32_t kSataSize = 0x00004000u;

constexpr uint32_t kOffP0Sctl = 0x12Cu;
constexpr uint32_t kOffP0Phycr = 0x178u;

constexpr uint32_t kSctlDetMask = 0xFu;
constexpr uint32_t kSctlDetComreset = 0x1u;
constexpr uint32_t kPhycrCrStrobes = 0x000F0000u;

class Imx6Sata final : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        if (!bd || !SocId::IsImx6(bd->GetSocId())) return false;
        auto* variant = emu_.TryGet<Imx6Variant>();
        return variant && variant->Family() == Imx6Family::DualQuad;
    }
    void OnReady() override {
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
    }

    uint32_t MmioBase() const override { return kSataBase; }
    uint32_t MmioSize() const override { return kSataSize; }

    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - kSataBase;
        if (off == kOffP0Sctl) return p0_sctl_;
        if (off == kOffP0Phycr) return p0_phycr_;
        HaltUnsupportedAccess("imx6-sata read32 unmodelled register", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - kSataBase;
        if (off == kOffP0Sctl) {
            if ((value & kSctlDetMask) == kSctlDetComreset)
                HaltUnsupportedAccess("imx6-sata P0SCTL COMRESET", addr, value);
            p0_sctl_ = value;
            return;
        }
        if (off == kOffP0Phycr) {
            if ((value & kPhycrCrStrobes) != 0u)
                HaltUnsupportedAccess("imx6-sata P0PHYCR control-register strobe", addr, value);
            p0_phycr_ = value;
            return;
        }
        HaltUnsupportedAccess("imx6-sata write32 unmodelled register", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write("p0_sctl", p0_sctl_);
        w.Write("p0_phycr", p0_phycr_);
    }
    void RestoreState(StateReader& r) override {
        r.Read("p0_sctl", p0_sctl_);
        r.Read("p0_phycr", p0_phycr_);
    }

private:
    void Reset() {
        p0_sctl_ = 0u;
        p0_phycr_ = 0u;
    }

    uint32_t p0_sctl_ = 0u;
    uint32_t p0_phycr_ = 0u;
};

}

REGISTER_SERVICE(Imx6Sata);

}
