#pragma once

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_gic.h"
#include "imx6_gpio_bus.h"
#include "imx6_gpio_source.h"
#include "imx6_gpio_pads.h"
#include "imx6_iomuxc.h"
#include "imx6_variant.h"
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_id.h"

namespace cerfcore {

namespace cerf_imx6_gpio_detail {

template <uint32_t kBase> class Imx6Gpio : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        const Imx6GpioPadTable& pads = Imx6GpioPadsFor(emu_.Get<Imx6Variant>().Family());
        for (uint32_t pin = 0; pin < 32u; ++pin) pads_[pin] = FindImx6GpioPad(pads, BankIndex() + 1u, pin);
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<Imx6GpioBus>().RegisterBank(
            MmioBase(), [this] { SourceChanged(); }, [this](uint32_t pin) { BeforePadChange(pin); },
            [this](uint32_t pin) { AfterPadChange(pin); });
        LatchPads(~0u);
        driven_level_ = SamplePads().level;
        last_driven_ = DrivenPins();
        UpdateIrq();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) {
                UpdateIrq();
                return;
            }
            PostResetEffects();
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        HaltUnsupportedAccess("imx6-gpio byte read", addr, 0);
    }
    uint16_t ReadHalf(uint32_t addr) override {
        HaltUnsupportedAccess("imx6-gpio half read", addr, 0);
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if (off <= kEdgeSel && (off & 3u) == 0) {
            return ReadRegister(off);
        }
        HaltUnsupportedAccess("read32", addr, 0);
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        HaltUnsupportedAccess("imx6-gpio byte write", addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        HaltUnsupportedAccess("imx6-gpio half write", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if (off <= kEdgeSel && (off & 3u) == 0) {
            WriteRegister(off, value);
            return;
        }
        HaltUnsupportedAccess("write32", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.WriteBytes("regs", regs_, sizeof(regs_));
        w.Write("retained", retained_);
        w.Write("retained_valid", retained_valid_);
        w.Write("driven_level", driven_level_);
        emu_.Get<Imx6GpioBus>().SaveSources(MmioBase(), w);
    }
    void RestoreState(StateReader& r) override {
        r.ReadBytes("regs", regs_, sizeof(regs_));
        r.Read("retained", retained_);
        r.Read("retained_valid", retained_valid_);
        r.Read("driven_level", driven_level_);
        emu_.Get<Imx6GpioBus>().RestoreSources(MmioBase(), r);
    }
    void PostRestore() override {
        if (Imx6GpioInputSource* s = Source())
            s->OnEffectiveOutputs(regs_[kDr >> 2], regs_[kGdir >> 2]);
        emu_.Get<Imx6GpioBus>().PostRestoreSources(MmioBase());
        last_driven_ = DrivenPins();
        UpdateIrq();
    }
    void Reset() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        retained_ = 0u;
        retained_valid_ = 0u;
    }
    void PostResetEffects() {
        if (Imx6GpioInputSource* s = Source()) {
            s->OnControllerReset();
            s->OnEffectiveOutputs(regs_[kDr >> 2], regs_[kGdir >> 2]);
        }
        emu_.Get<Imx6GpioBus>().PostRestoreSources(MmioBase());
        driven_level_ = SamplePads().level;
        last_driven_ = DrivenPins();
        UpdateIrq();
    }

private:
    static constexpr uint32_t kDr = 0x00u;
    static constexpr uint32_t kGdir = 0x04u;
    static constexpr uint32_t kPsr = 0x08u;
    static constexpr uint32_t kIcr1 = 0x0Cu;
    static constexpr uint32_t kIcr2 = 0x10u;
    static constexpr uint32_t kImr = 0x14u;
    static constexpr uint32_t kIsr = 0x18u;
    static constexpr uint32_t kEdgeSel = 0x1Cu;

    uint32_t ReadRegister(uint32_t off) {
        switch (off) {
        case kDr: return ReadDataRegister();
        case kGdir: return regs_[kGdir >> 2];
        case kPsr: return ReadPadStatus();
        case kIcr1:
        case kIcr2:
        case kImr:
        case kEdgeSel: return regs_[off >> 2];
        case kIsr: UpdateLevelSensitiveStatus(true); return regs_[kIsr >> 2];
        default: HaltUnsupportedAccess("imx6-gpio read32 unmodelled register", MmioBase() + off, 0);
        }
    }

    struct PadLevels {
        uint32_t level = 0u;
        uint32_t known = 0u;
    };

    PadLevels SamplePads() const {
        PadLevels out;
        const uint32_t gdir = regs_[kGdir >> 2];
        const uint32_t driven = DrivenPins();
        const uint32_t source_levels = driven ? Source()->ApplyPadInputs(0u) : 0u;
        auto& iomuxc = emu_.Get<Imx6Iomuxc>();
        for (uint32_t pin = 0; pin < 32u; ++pin) {
            const uint32_t bit = 1u << pin;
            const Imx6GpioPad* pad = pads_[pin];
            bool high = false;
            bool known = true;
            if (!pad || (iomuxc.PadRegister(pad->mux_offset) & kImx6PadMuxMode) != pad->gpio_mode) {
                high = false;
            } else if (gdir & bit) {
                high = (regs_[kDr >> 2] & bit) != 0u;
            } else if (driven & bit) {
                high = (source_levels & bit) != 0u;
            } else {
                const Imx6PadInputLevel input = ResolveImx6PadInputLevel(
                    iomuxc.PadRegister(pad->ctl_offset), (retained_ & bit) != 0u, (retained_valid_ & bit) != 0u);
                high = input.high;
                known = input.known;
            }
            if (known) out.known |= bit;
            if (known && high) out.level |= bit;
        }
        return out;
    }

    bool InGpioMode(uint32_t pin) const {
        const Imx6GpioPad* pad = pads_[pin];
        return pad && (emu_.Get<Imx6Iomuxc>().PadRegister(pad->mux_offset) & kImx6PadMuxMode) == pad->gpio_mode;
    }

    [[noreturn]] void HaltUnknownPads(uint32_t pins, const char* what) const {
        emu_.Get<Fatal>().Die("imx6-gpio%u %s: input pins 0x%08X float (no SoC pull, no keeper level)",
                              BankIndex() + 1u, what, pins);
    }

    uint32_t ReadPadStatus() const {
        const PadLevels pads = SamplePads();
        const uint32_t inputs = ~regs_[kGdir >> 2];
        if ((inputs & ~pads.known) != 0u) HaltUnknownPads(inputs & ~pads.known, "read");
        const uint32_t gdir = regs_[kGdir >> 2];
        return (regs_[kDr >> 2] & gdir) | (pads.level & ~gdir);
    }

    void LatchPads(uint32_t pins) {
        const PadLevels pads = SamplePads();
        for (uint32_t pin = 0; pin < 32u; ++pin) {
            const uint32_t bit = 1u << pin;
            if ((pins & bit) == 0u) continue;
            if ((pads.known & bit) && InGpioMode(pin)) {
                retained_ = (retained_ & ~bit) | (pads.level & bit);
                retained_valid_ |= bit;
            } else {
                retained_valid_ &= ~bit;
            }
        }
    }

    void BeforePadChange(uint32_t pin) {
        before_change_ = SamplePads();
        LatchPads(1u << pin);
    }

    void AfterPadChange(uint32_t) {
        ApplyEdges(before_change_, SamplePads(), ~DrivenPins());
        UpdateIrq();
    }

    void SourceChanged() {
        PadLevels before = SamplePads();
        before.level = driven_level_;
        const PadLevels after = SamplePads();
        const uint32_t driven = DrivenPins();
        ApplyEdges(before, after, driven | last_driven_);
        driven_level_ = after.level;
        last_driven_ = driven;
        UpdateIrq();
    }

    void ApplyEdges(const PadLevels& before, const PadLevels& after, uint32_t candidates) {
        const uint32_t pins = ~regs_[kGdir >> 2] & candidates;
        for (uint32_t pin = 0; pin < 32u; ++pin) {
            const uint32_t bit = 1u << pin;
            if ((pins & bit) == 0u) continue;
            const bool any = (regs_[kEdgeSel >> 2] & bit) != 0u;
            const uint32_t sense = InterruptSenseForPin(pin);
            if (!any && sense < 2u) continue;
            if (((before.known & after.known) & bit) == 0u) {
                if (regs_[kImr >> 2] & bit) HaltUnknownPads(bit, "edge interrupt");
                continue;
            }
            const bool was = (before.level & bit) != 0u;
            const bool now = (after.level & bit) != 0u;
            if (was != now && (any || (sense == 2u && now) || (sense == 3u && !now)))
                regs_[kIsr >> 2] |= bit;
        }
    }

    uint32_t DrivenPins() const {
        Imx6GpioInputSource* s = Source();
        return s ? s->DrivenPins() : 0u;
    }

    void WriteRegister(uint32_t off, uint32_t value) {
        switch (off) {
        case kPsr:
            HaltUnsupportedAccess("imx6-gpio write to the read-only PSR", MmioBase() + off, value);
        case kIsr:
            if (Imx6GpioInputSource* s = Source()) s->OnIsrClear(value);
            regs_[kIsr >> 2] &= ~value;
            UpdateIrq();
            return;
        case kImr:
            regs_[kImr >> 2] = value;
            UpdateIrq();
            return;
        case kIcr1:
        case kIcr2:
        case kEdgeSel:
            regs_[off >> 2] = value;
            UpdateIrq();
            return;
        case kDr:
        case kGdir: {
            const PadLevels before = SamplePads();
            LatchPads(~0u);
            regs_[off >> 2] = value;
            if (Imx6GpioInputSource* s = Source())
                s->OnEffectiveOutputs(regs_[kDr >> 2], regs_[kGdir >> 2]);
            ApplyEdges(before, SamplePads(), ~DrivenPins());
            UpdateIrq();
            return;
        }
        default: HaltUnsupportedAccess("imx6-gpio write32 unmodelled register", MmioBase() + off, value);
        }
    }

    uint32_t ReadDataRegister() {
        uint32_t value = ReadPadStatus();
        if (Imx6GpioInputSource* s = Source()) value = s->ApplyDataRead(value);
        return value;
    }

    uint32_t BankIndex() const { return (MmioBase() - 0x0209C000u) / 0x4000u; }

    uint32_t GpioSpiLow16() const { return 66u + BankIndex() * 2u; }

    Imx6GpioInputSource* Source() const { return emu_.Get<Imx6GpioBus>().Find(MmioBase()); }

    uint32_t InterruptSenseForPin(uint32_t pin) const {
        const uint32_t icr = pin < 16u ? regs_[kIcr1 >> 2] : regs_[kIcr2 >> 2];
        return (icr >> (2u * (pin & 15u))) & 3u;
    }

    void UpdateLevelSensitiveStatus(bool guest_reads_isr = false) {
        const PadLevels pads = SamplePads();
        const uint32_t inputs = ~regs_[kGdir >> 2];
        uint32_t level_mask = 0;
        uint32_t active_mask = 0;
        uint32_t unknown = 0;
        for (uint32_t pin = 0; pin < 32u; ++pin) {
            const uint32_t bit = 1u << pin;
            if ((inputs & bit) == 0u) continue;
            if (regs_[kEdgeSel >> 2] & bit) continue;
            const uint32_t sense = InterruptSenseForPin(pin);
            if (sense > 1u) continue;
            if ((pads.known & bit) == 0u) {
                unknown |= bit;
                continue;
            }
            level_mask |= bit;
            const bool high = (pads.level & bit) != 0u;
            if ((sense == 0u && !high) || (sense == 1u && high)) active_mask |= bit;
        }
        if (unknown & (guest_reads_isr ? ~0u : regs_[kImr >> 2])) HaltUnknownPads(unknown, "level interrupt");
        regs_[kIsr >> 2] = (regs_[kIsr >> 2] & ~level_mask) | active_mask;
        if (Imx6GpioInputSource* s = Source()) regs_[kIsr >> 2] |= s->PendingIsr();
    }

    void UpdateIrq() {
        UpdateLevelSensitiveStatus();
        auto& gic = emu_.Get<Imx6Gic>();

        const uint32_t pending = regs_[kIsr >> 2] & regs_[kImr >> 2];
        if (pending & 0x0000FFFFu)
            gic.AssertSpi(static_cast<int>(GpioSpiLow16()));
        else
            gic.DeAssertSpi(static_cast<int>(GpioSpiLow16()));
        if (pending & 0xFFFF0000u)
            gic.AssertSpi(static_cast<int>(GpioSpiLow16() + 1u));
        else
            gic.DeAssertSpi(static_cast<int>(GpioSpiLow16() + 1u));
    }

    uint32_t regs_[(kEdgeSel >> 2) + 1u]{};
    const Imx6GpioPad* pads_[32]{};
    uint32_t retained_ = 0u;
    uint32_t retained_valid_ = 0u;
    uint32_t driven_level_ = 0u;
    uint32_t last_driven_ = 0u;
    PadLevels before_change_;

};

}

using cerf_imx6_gpio_detail::Imx6Gpio;

}
