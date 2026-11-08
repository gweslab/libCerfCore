#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include "imx31_id.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "../freescale_module_clocks.h"
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x43F9C000u;
constexpr uint32_t kSize = 0x00000008u;

constexpr uint32_t kControl     = 0x00u;
constexpr uint32_t kTimeDivider = 0x02u;
constexpr uint32_t kReset       = 0x04u;

constexpr uint16_t kCtrlRpp  = 1u << 7;
constexpr uint16_t kCtrlWr0  = 1u << 5;
constexpr uint16_t kCtrlWr1  = 1u << 4;
constexpr uint16_t kCtrlRdst = 1u << 3;
constexpr uint16_t kCtrlStrobes = kCtrlRpp | kCtrlWr0 | kCtrlWr1;

class Imx31Owire : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetSocId() == SocId::Imx31;
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        module_clocks_->RegisterGateListener([this] { RunHeldStrobes(); });
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            control_      = 0;
            time_divider_ = 0;
            reset_        = 0;
        });
        emu_.Get<PeripheralDispatcher>().Register(this);
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return kSize; }

    uint16_t ReadHalf(uint32_t addr) override { return ReadReg16(addr - kBase); }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        WriteReg16(addr - kBase, value);
    }
    uint8_t ReadByte(uint32_t addr) override {
        const uint16_t v = ReadReg16((addr - kBase) & ~1u);
        return ((addr & 1u) ? (v >> 8) : v) & 0xFFu;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = (addr - kBase) & ~1u;
        uint16_t v = ReadReg16(off);
        v = (addr & 1u) ? ((v & 0x00FFu) | (uint16_t(value) << 8))
                        : ((v & 0xFF00u) | value);
        WriteReg16(off, v);
    }

    void SaveState(StateWriter& w) override {
        w.Write("control", control_); w.Write("time_divider", time_divider_); w.Write("reset", reset_);
    }
    void RestoreState(StateReader& r) override {
        r.Read("control", control_); r.Read("time_divider", time_divider_); r.Read("reset", reset_);
    }

private:
    uint16_t ReadReg16(uint32_t off) {
        switch (off) {
            case kControl:     return control_;
            case kTimeDivider: return time_divider_;
            case kReset:       return reset_;
        }
        HaltUnsupportedAccess("ReadReg16", kBase + off, 0);
    }
    void WriteReg16(uint32_t off, uint16_t value) {
        switch (off) {
            case kControl:
                control_ |= value & kCtrlStrobes;
                RunHeldStrobes();
                return;
            case kTimeDivider: time_divider_ = value; return;
            case kReset:
                reset_ = value;
                if (value & 1u) control_ = 0;
                return;
        }
        HaltUnsupportedAccess("WriteReg16", kBase + off, value);
    }

    void RunHeldStrobes() {
        if (!module_clocks_->Running(FreescaleModule::kOwire)) return;
        if (control_ & kCtrlWr1) control_ |= kCtrlRdst;
        control_ &= static_cast<uint16_t>(~kCtrlStrobes);
    }

    uint16_t control_      = 0;
    uint16_t time_divider_ = 0;
    uint16_t reset_        = 0;
    FreescaleModuleClocks* module_clocks_ = nullptr;
};

}

REGISTER_SERVICE(Imx31Owire);

}
