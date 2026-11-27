#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kTcsrBase = 0xAB600000u;
constexpr uint32_t kTcsrSize = 0x00001000u;

constexpr uint32_t kRegAppsSecop  = 0x38u;
constexpr uint32_t kAppsSecopBits = 0x4u;

class Msm8255Tcsr : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<GuestCpuReset>().RegisterResetListener(
            [this](ResetLineKind) { apps_secop_.store(0u, std::memory_order_release); });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kTcsrBase; }
    uint32_t MmioSize() const override { return kTcsrSize; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr - MmioBase() == kRegAppsSecop) return apps_secop_.load(std::memory_order_acquire);
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() == kRegAppsSecop && (value & ~kAppsSecopBits) == 0u) {
            apps_secop_.store(value, std::memory_order_release);
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("tcsr_apps_secop", apps_secop_.load(std::memory_order_acquire));
    }

    void RestoreState(StateReader& r) override {
        uint32_t apps_secop = 0u;
        r.Read("tcsr_apps_secop", apps_secop);
        apps_secop_.store(apps_secop, std::memory_order_release);
    }

private:
    std::atomic<uint32_t> apps_secop_{0u};
};

}

REGISTER_SERVICE(Msm8255Tcsr);

}
