#include "../uart16550/uart16550.h"
#include "isa_debug_port_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kUartOffset = 0xC0u;
constexpr uint32_t kRegStride  = 2u;

class IsaDebugUart : public Uart16550 {
public:
    explicit IsaDebugUart(CerfEmulator& emu)
        : Uart16550(emu, Config{0x0Fu,
                                0u,
                                0u}) {}

    bool ShouldRegister() override {
        return emu_.TryGet<IsaDebugPortConfig>() != nullptr;
    }

    void OnReady() override {
        mmio_base_ = emu_.Get<IsaDebugPortConfig>().IsaWindowBase() + kUartOffset;
        Uart16550::OnReady();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            if (kind == ResetLineKind::Rtc) {
                ResetRegisters();
            } else {
                MasterReset();
            }
        });
    }

    uint32_t MmioBase() const override { return mmio_base_; }
    uint32_t MmioSize() const override { return 0x10u; }

    uint8_t ReadByte(uint32_t addr) override {
        if ((addr - mmio_base_) % kRegStride != 0u) HaltUnsupportedAccess("ReadByte", addr, 0);
        return static_cast<uint8_t>(Uart16550::ReadWord(addr) & 0xFFu);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        if ((addr - mmio_base_) % kRegStride != 0u) HaltUnsupportedAccess("WriteByte", addr, value);
        Uart16550::WriteWord(addr, value);
    }

    uint32_t ReadWord(uint32_t addr) override { return Peripheral::ReadWord(addr); }
    void WriteWord(uint32_t addr, uint32_t value) override { Peripheral::WriteWord(addr, value); }

protected:
    uint32_t    RegStride() const override { return kRegStride; }
    const char* Name()      const override { return "COM"; }

    void SetInterruptLine(bool pending) override {
        if (!pending) return;
        emu_.Get<Fatal>().Die("IsaDebugUart at 0x%08X raised its interrupt line; its routing "
                              "to the interrupt controller is not modeled", mmio_base_);
    }

private:
    uint32_t mmio_base_ = 0;
};

}

REGISTER_SERVICE(IsaDebugUart);

}
