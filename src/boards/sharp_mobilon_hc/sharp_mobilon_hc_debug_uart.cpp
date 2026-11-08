#include "../../peripherals/uart16550/uart16550.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/boards/board_context.h>
#include "sharp_mobilon_hc_family_id.h"
#include "sharp_mobilon_hc_cmtt.h"

#include <cstdint>

namespace cerfcore {

namespace {

class SharpMobilonHcDebugUart : public Uart16550 {
public:
    explicit SharpMobilonHcDebugUart(CerfEmulator& emu)
        : Uart16550(emu, Config{}, Clock{1843200u, 1u}) {}

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::SharpMobilonHc;
    }

    uint32_t MmioBase() const override { return 0x10800000u; }
    uint32_t MmioSize() const override { return 0x100u; }

    uint8_t ReadByte(uint32_t addr) override {
        uint32_t idx;
        if (CoreIndex(addr, idx)) return ReadReg8(idx);
        if (addr - MmioBase() == 0x03u) {
            return emu_.Get<SharpMobilonHcCmtt>().ReadStatusByte();
        }
        HaltUnsupportedAccess("ReadByte", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        uint32_t idx;
        if (CoreIndex(addr, idx)) { WriteReg8(idx, value); return; }
        if (addr - MmioBase() == 0x42u) {
            if (value != 0x44u) HaltUnsupportedAccess("CS3 +0x42 chip byte", addr, value);
            return;
        }
        HaltUnsupportedAccess("WriteByte", addr, value);
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if (addr - MmioBase() == 0x00u) {
            emu_.Get<SharpMobilonHcCmtt>().WriteDataPort(value);
            return;
        }
        HaltUnsupportedAccess("WriteWord", addr, value);
    }
    uint32_t ReadWord(uint32_t addr) override {
        if (addr - MmioBase() == 0x00u) {
            return emu_.Get<SharpMobilonHcCmtt>().ReadDataPort();
        }
        HaltUnsupportedAccess("ReadWord", addr, 0);
    }

protected:
    uint32_t    RegStride() const override { return 4u; }
    const char* Name()      const override { return "CS3DBG"; }

    void SetInterruptLine(bool pending) override {
        if (!pending) return;
        emu_.Get<Fatal>().Die("CS3 debug UART raised an interrupt; no INTC routing modeled");
    }

private:
    static constexpr uint32_t kCoreOff = 0x20u;

    bool CoreIndex(uint32_t addr, uint32_t& idx) const {
        const uint32_t off = addr - MmioBase();
        if (off < kCoreOff) return false;
        const uint32_t rel = off - kCoreOff;
        if (rel % RegStride() != 0u) return false;
        const uint32_t i = rel / RegStride();
        if (i >= 8u) return false;
        idx = i;
        return true;
    }
};

}  /* namespace */

REGISTER_SERVICE(SharpMobilonHcDebugUart);

}
