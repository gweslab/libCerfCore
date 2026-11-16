#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>

#include <cerfcore/core/cerf_emulator.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_mmio_lane.h"
#include "../freescale_module_clocks.h"

#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace cerf_imx6_pwm_detail {

template <uint32_t kBase, FreescaleModule kModule> class Imx6Pwm : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        module_clocks_ = &emu_.Get<FreescaleModuleClocks>();
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
    }

    uint32_t MmioBase() const override { return kBase; }

private:
    void Reset() {
        control_ = 0u;
        sample_ = 0u;
    }

    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        module_clocks_->RequireRunning(kModule, "register read");
        const uint32_t off = addr - MmioBase();
        if (off == kOffCr) return control_;
        if (off == kOffSar && (control_ & kCrEnable) != 0u) return sample_;
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        module_clocks_->RequireRunning(kModule, "register write");
        const uint32_t off = addr - MmioBase();
        if (off == kOffCr) {
            if ((value & ~kCrEnable) != 0u)
                HaltUnsupportedAccess("imx6-pwm PWMCR clock, prescaler, output and reset control", addr, value);
            control_ = value;
            return;
        }
        if (off == kOffSar) {
            if ((value & ~kSarSample) != 0u)
                HaltUnsupportedAccess("imx6-pwm PWMSAR reserved bit write", addr, value);
            sample_ = value & kSarSample;
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write("control", control_);
        w.Write("sample", sample_);
    }
    void RestoreState(StateReader& r) override {
        r.Read("control", control_);
        r.Read("sample", sample_);
    }

    static constexpr uint32_t kOffCr = 0x00u;
    static constexpr uint32_t kOffSar = 0x0Cu;
    static constexpr uint32_t kCrEnable = 1u << 0;
    static constexpr uint32_t kSarSample = 0xFFFFu;

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == kOffSar) {
            WriteWord(lane.address, lane.Merge(sample_));
            return;
        }
        if (off == kOffCr) {
            WriteWord(lane.address, lane.Merge(control_));
            return;
        }
        HaltUnsupportedAccess("write lane", lane.address, lane.value);
    }

    uint32_t control_ = 0;
    uint32_t sample_ = 0;
    FreescaleModuleClocks* module_clocks_ = nullptr;
};

}

using cerf_imx6_pwm_detail::Imx6Pwm;

}
