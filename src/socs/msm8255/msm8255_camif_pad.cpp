#include <cerfcore/boards/board_context.h>
#include "msm8255_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <atomic>
#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kCamifPadBase  = 0xAB000000u;
constexpr uint32_t kCamifPadSize  = 0x00000400u;
constexpr uint32_t kRegCamifCfg   = 0x00u;
constexpr uint32_t kCamifCfgMask  = 0x001FFFFFu;
constexpr uint32_t kCamifCfgReset = 0u;

class Msm8255CamifPad : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            cfg_.store(kCamifCfgReset, std::memory_order_release);
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kCamifPadBase; }
    uint32_t MmioSize() const override { return kCamifPadSize; }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr - kCamifPadBase == kRegCamifCfg) return cfg_.load(std::memory_order_acquire);
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - kCamifPadBase == kRegCamifCfg && (value & ~kCamifCfgMask) == 0u) {
            cfg_.store(value, std::memory_order_release);
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write<uint32_t>("camif_cfg", cfg_.load(std::memory_order_acquire));
    }

    void RestoreState(StateReader& r) override {
        uint32_t cfg = kCamifCfgReset;
        r.Read("camif_cfg", cfg);
        cfg_.store(cfg, std::memory_order_release);
    }

private:
    std::atomic<uint32_t> cfg_{kCamifCfgReset};
};

}

REGISTER_SERVICE(Msm8255CamifPad);

}
