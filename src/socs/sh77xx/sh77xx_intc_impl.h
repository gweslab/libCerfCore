#pragma once

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <cerfengine/sh/sh_cpu_state.h>
#include <cerfengine/sh/sh_interrupt_channel.h>
#include <cerfengine/sh/sh_interrupt_host.h>
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "sh77xx_intc.h"
#include "sh77xx_nmi_pin.h"
#include "sh77xx_rtc.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <string_view>

namespace cerfcore {

namespace sh77xx_intc {

struct Model {
    bool irls;
};

inline constexpr uint32_t kArea1Base = 0x04000000u;
inline constexpr uint32_t kArea1Size = 0x1Cu;
inline constexpr uint32_t kArea7Base = 0x1FFFFEE0u;

inline constexpr uint32_t kOffIntevt2 = 0x00u;
inline constexpr uint32_t kOffIrr0    = 0x04u;
inline constexpr uint32_t kOffIrr1    = 0x06u;
inline constexpr uint32_t kOffIrr2    = 0x08u;
inline constexpr uint32_t kOffIcr1    = 0x10u;
inline constexpr uint32_t kOffIcr2    = 0x12u;
inline constexpr uint32_t kOffPinter  = 0x14u;
inline constexpr uint32_t kOffIprc    = 0x16u;
inline constexpr uint32_t kOffIprd    = 0x18u;
inline constexpr uint32_t kOffIpre    = 0x1Au;
inline constexpr uint32_t kOffIcr0    = 0x0u;
inline constexpr uint32_t kOffIpra    = 0x2u;
inline constexpr uint32_t kOffIprb    = 0x4u;

enum Ipr : uint8_t { kIpra, kIprb, kIprc, kIprd, kIpre, kIprCount };

inline constexpr uint16_t kIcr0Nmil     = 0x8000u;
inline constexpr uint16_t kIcr1Reset    = 0x4000u;
inline constexpr uint16_t kIcr1Mai      = 0x8000u;
inline constexpr uint16_t kIcr1IrqLevel = 0x4000u;
inline constexpr uint16_t kIcr1Irlsen   = 0x1000u;
inline constexpr uint16_t kIprbReserved = 0x000Fu;

inline constexpr uint32_t kIrqPins       = 6u;
inline constexpr uint32_t kIrlPins       = 4u;
inline constexpr uint32_t kPintBit0      = static_cast<uint32_t>(Sh77xxIntcPin::kPint0);
inline constexpr uint32_t kIrlsBit0      = static_cast<uint32_t>(Sh77xxIntcPin::kIrls0);
inline constexpr uint32_t kAllPinsHigh   = (1u << static_cast<uint32_t>(Sh77xxIntcPin::kCount)) - 1u;
inline constexpr uint32_t kSenseFalling  = 0u;
inline constexpr uint32_t kSenseRising   = 1u;
inline constexpr uint32_t kSenseLow      = 2u;
inline constexpr uint32_t kSenseReserved = 3u;
inline constexpr uint32_t kIrlNone       = 0xFu;
inline constexpr uint32_t kPintPinMask    = 0xFFFFu;
inline constexpr uint32_t kPintGroupMask  = 0x00FFu;
inline constexpr uint32_t kPintGroupShift = 8u;
inline constexpr uint8_t  kIrr0Pint0R    = 0x80u;
inline constexpr uint8_t  kIrr0Pint1R    = 0x40u;
inline constexpr uint32_t kIrr0IrqMask   = 0x3Fu;
inline constexpr uint32_t kIrr1Mask      = 0xFFu;
inline constexpr uint32_t kIrr2Shift     = 8u;
inline constexpr uint32_t kIrr2Mask      = 0x1Fu;
inline constexpr uint32_t kLevelCodeBase = 0x200u;
inline constexpr uint32_t kLevelCodeStep = 0x20u;
inline constexpr uint32_t kMaxLevel      = 15u;

inline constexpr uint32_t kStandbyWakeIrqPins = 5u;

constexpr uint32_t SourceBit(Sh77xxIntcSource source) { return 1u << static_cast<uint32_t>(source); }

inline constexpr uint32_t kStandbyWakeModules =
    SourceBit(Sh77xxIntcSource::kTuni0) | SourceBit(Sh77xxIntcSource::kTuni1) | SourceBit(Sh77xxIntcSource::kTuni2) |
    SourceBit(Sh77xxIntcSource::kTicpi2) | SourceBit(Sh77xxIntcSource::kAti) | SourceBit(Sh77xxIntcSource::kPri) |
    SourceBit(Sh77xxIntcSource::kCui);

enum class Scope : uint8_t { kAll, kStandbyWake };

struct SourceRow {
    Ipr      ipr;
    uint8_t  shift;
    bool     level_coded;
    uint16_t intevt2;
};

inline constexpr SourceRow kIrqRows[kIrqPins] = {
    {kIprc, 0u, true, 0x600u}, {kIprc, 4u, true, 0x620u}, {kIprc, 8u, true, 0x640u},
    {kIprc, 12u, true, 0x660u}, {kIprd, 0u, true, 0x680u}, {kIprd, 4u, true, 0x6A0u},
};

inline constexpr SourceRow kPintRows[2] = {{kIprd, 12u, true, 0x700u}, {kIprd, 8u, true, 0x720u}};

inline constexpr SourceRow kModuleRows[] = {
    {kIpre, 12u, true, 0x800u},  {kIpre, 12u, true, 0x820u},  {kIpre, 12u, true, 0x840u},
    {kIpre, 12u, true, 0x860u},  {kIpre, 8u, true, 0x880u},   {kIpre, 8u, true, 0x8A0u},
    {kIpre, 8u, true, 0x8C0u},   {kIpre, 8u, true, 0x8E0u},   {kIpre, 4u, true, 0x900u},
    {kIpre, 4u, true, 0x920u},   {kIpre, 4u, true, 0x940u},   {kIpre, 4u, true, 0x960u},
    {kIpre, 0u, true, 0x980u},   {kIpra, 12u, false, 0x400u}, {kIpra, 8u, false, 0x420u},
    {kIpra, 4u, false, 0x440u},  {kIpra, 4u, false, 0x460u},  {kIpra, 0u, false, 0x480u},
    {kIpra, 0u, false, 0x4A0u},  {kIpra, 0u, false, 0x4C0u},  {kIprb, 4u, false, 0x4E0u},
    {kIprb, 4u, false, 0x500u},  {kIprb, 4u, false, 0x520u},  {kIprb, 4u, false, 0x540u},
    {kIprb, 12u, false, 0x560u}, {kIprb, 8u, false, 0x580u},  {kIprb, 8u, false, 0x5A0u},
};

static_assert(std::size(kModuleRows) == static_cast<size_t>(Sh77xxIntcSource::kCount),
              "every SH77xx on-chip interrupt source has a row");

struct Selection {
    uint32_t level   = 0u;
    uint32_t intevt  = 0u;
    uint32_t intevt2 = 0u;
};

constexpr uint32_t LevelCode(uint32_t level) { return kLevelCodeBase + (kMaxLevel - level) * kLevelCodeStep; }

template <const std::string_view& Soc, const Model& M>
class Sh77xxIntcBase : public Sh77xxIntc, public cerfengine::ShInterruptHost {
public:
    using Sh77xxIntc::Sh77xxIntc;

    bool ShouldRegister() override { return this->emu_.template Get<BoardContext>().GetSocId() == Soc; }

    void OnReady() override {
        nmi_     = &this->emu_.template Get<Sh77xxNmiPin>();
        reset_   = &this->emu_.template Get<GuestCpuReset>();
        channel_ = &this->emu_.template Get<EngineBridge>().Engine().template Get<cerfengine::ShInterruptChannel>();
        ResetRegisters();
        channel_->SetHost(this);
        this->emu_.template Get<PeripheralDispatcher>().Register(this);
        reset_->RegisterResetListener([this](ResetLineKind) {
            std::lock_guard<std::mutex> lk(mtx_);
            ResetRegisters();
            PublishLocked();
        });
    }

    uint32_t MmioBase() const override { return kArea1Base; }
    uint32_t MmioSize() const override { return kArea1Size; }

    void SetSourceRequest(Sh77xxIntcSource source, bool asserted) override {
        std::lock_guard<std::mutex> lk(mtx_);
        const uint32_t bit = 1u << static_cast<uint32_t>(source);
        if (((requests_ & bit) != 0u) == asserted) return;
        requests_ ^= bit;
        PublishLocked();
    }

    void SetPinLevel(Sh77xxIntcPin pin, bool high) override {
        std::lock_guard<std::mutex> lk(mtx_);
        const uint32_t n        = static_cast<uint32_t>(pin);
        const bool     was_high = PinHigh(n);
        if (was_high == high) return;
        pin_high_ ^= 1u << n;
        if (n < kIrqPins && !IrqIsIrl(n) && !reset_->Delivering()) {
            const uint32_t sense = IrqSense(n);
            if ((sense == kSenseFalling && was_high) || (sense == kSenseRising && !was_high)) irq_latch_ |= 1u << n;
        }
        PublishLocked();
    }

    bool PinLevel(Sh77xxIntcPin pin) override {
        std::lock_guard<std::mutex> lk(mtx_);
        return PinHigh(static_cast<uint32_t>(pin));
    }

    uint32_t AcceptInterrupt(uint32_t sr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        const Selection s = SelectLocked(Scope::kAll);
        if (!cerfengine::ShSrAcceptsInterrupt(sr, s.level)) return 0u;
        intevt2_ = s.intevt2;
        return s.intevt;
    }

    uint32_t StandbyWakeLevel() override {
        const Sh77xxRtc* rtc = this->emu_.template TryGet<Sh77xxRtc>();
        if (rtc == nullptr) {
            this->emu_.template Get<Fatal>().Die("Sh77xxIntc: a standby wake on %.*s needs the RTC oscillator, "
                                                 "and no RTC model exists for it",
                                                 static_cast<int>(Soc.size()), Soc.data());
        }
        if (!rtc->OscillatorRunning()) return 0u;
        std::lock_guard<std::mutex> lk(mtx_);
        return SelectLocked(Scope::kStandbyWake).level;
    }

    bool WakesStandby(Sh77xxIntcSource source) override {
        if ((kStandbyWakeModules & SourceBit(source)) == 0u) return false;
        std::lock_guard<std::mutex> lk(mtx_);
        if (AllMaskedLocked()) return false;
        return channel_->LevelEndsSleep(RowLevel(kModuleRows[static_cast<size_t>(source)]));
    }

    uint8_t ReadByte(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kArea1Base) {
        case kOffIrr0: return ReadIrr0Locked(addr);
        case kOffIrr1: return static_cast<uint8_t>(requests_ & kIrr1Mask);
        case kOffIrr2: return static_cast<uint8_t>((requests_ >> kIrr2Shift) & kIrr2Mask);
        default:       this->HaltUnsupportedAccess("ReadByte", addr, 0u);
        }
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        const uint32_t off = addr - kArea1Base;
        if (off == kOffIrr1 || off == kOffIrr2) return;
        if (off != kOffIrr0) this->HaltUnsupportedAccess("WriteByte", addr, value);
        std::lock_guard<std::mutex> lk(mtx_);
        const uint32_t cleared = ~static_cast<uint32_t>(value) & irr0_read_ones_ & kIrr0IrqMask;
        irq_latch_ &= ~cleared;
        irr0_read_ones_ &= ~cleared;
        PublishLocked();
    }

    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kArea1Base) {
        case kOffIrr1:
        case kOffIrr2:   return 0u;
        case kOffIcr1:   return icr1_;
        case kOffIcr2:   return icr2_;
        case kOffPinter: return pinter_;
        case kOffIprc:   return ipr_[kIprc];
        case kOffIprd:   return ipr_[kIprd];
        case kOffIpre:   return ipr_[kIpre];
        default:         this->HaltUnsupportedAccess("ReadHalf", addr, 0u);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kArea1Base) {
        case kOffIrr1:
        case kOffIrr2:   return;
        case kOffIcr1:   WriteIcr1Locked(addr, value); break;
        case kOffIcr2:   icr2_ = value; break;
        case kOffPinter: pinter_ = value; break;
        case kOffIprc:   ipr_[kIprc] = value; break;
        case kOffIprd:   ipr_[kIprd] = value; break;
        case kOffIpre:   ipr_[kIpre] = value; break;
        default:         this->HaltUnsupportedAccess("WriteHalf", addr, value);
        }
        PublishLocked();
    }

    uint32_t ReadWord(uint32_t addr) override {
        if (addr - kArea1Base != kOffIntevt2) this->HaltUnsupportedAccess("ReadWord", addr, 0u);
        std::lock_guard<std::mutex> lk(mtx_);
        return intevt2_;
    }

    uint16_t ReadArea7Half(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kArea7Base) {
        case kOffIcr0: return nmi_->High() ? kIcr0Nmil : 0u;
        case kOffIpra: return ipr_[kIpra];
        case kOffIprb: return ipr_[kIprb];
        default:       this->HaltUnsupportedAccess("ReadHalf", addr, 0u);
        }
    }

    void WriteArea7Half(uint32_t addr, uint16_t value) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - kArea7Base) {
        case kOffIpra:
            ipr_[kIpra] = value;
            break;
        case kOffIprb:
            if ((value & kIprbReserved) != 0u) this->HaltUnsupportedAccess("WriteHalf IPRB bits 3-0", addr, value);
            ipr_[kIprb] = value;
            break;
        default:
            this->HaltUnsupportedAccess("WriteHalf", addr, value);
        }
        PublishLocked();
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(mtx_);
        w.Write("icr1", icr1_);
        w.Write("icr2", icr2_);
        w.Write("pinter", pinter_);
        w.WriteBytes("ipr", ipr_, sizeof(ipr_));
        w.Write("requests", requests_);
        w.Write("intevt2", intevt2_);
        w.Write("pin_high", pin_high_);
        w.Write("irq_latch", irq_latch_);
        w.Write("irr0_read_ones", irr0_read_ones_);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(mtx_);
        r.Read("icr1", icr1_);
        r.Read("icr2", icr2_);
        r.Read("pinter", pinter_);
        r.ReadBytes("ipr", ipr_, sizeof(ipr_));
        r.Read("requests", requests_);
        r.Read("intevt2", intevt2_);
        r.Read("pin_high", pin_high_);
        r.Read("irq_latch", irq_latch_);
        r.Read("irr0_read_ones", irr0_read_ones_);
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lk(mtx_);
        PublishLocked();
    }

private:
    void ResetRegisters() {
        icr1_           = kIcr1Reset;
        icr2_           = 0u;
        pinter_         = 0u;
        irq_latch_      = 0u;
        irr0_read_ones_ = 0u;
        for (uint16_t& ipr : ipr_) ipr = 0u;
    }

    void WriteIcr1Locked(uint32_t addr, uint16_t value) {
        if (!M.irls) value = static_cast<uint16_t>(value & ~kIcr1Irlsen);
        for (uint32_t n = 0; n < kIrqPins; ++n) {
            if (((value >> (2u * n)) & 3u) == kSenseReserved) {
                this->HaltUnsupportedAccess("WriteHalf ICR1 IRQ sense 11", addr, value);
            }
        }
        icr1_ = value;
    }

    uint8_t ReadIrr0Locked(uint32_t addr) {
        if ((icr1_ & kIcr1IrqLevel) != 0u) this->HaltUnsupportedAccess("ReadByte IRR0 with ICR1.IRQLVL set", addr, 0u);
        uint8_t value = 0u;
        for (uint32_t n = 0; n < kIrqPins; ++n) {
            if (IrqRequest(n)) value = static_cast<uint8_t>(value | (1u << n));
        }
        const uint32_t pint = PintRequests();
        if ((pint & kPintGroupMask) != 0u) value |= kIrr0Pint0R;
        if ((pint >> kPintGroupShift) != 0u) value |= kIrr0Pint1R;
        irr0_read_ones_ |= value & kIrr0IrqMask;
        return value;
    }

    bool     PinHigh(uint32_t n) const { return ((pin_high_ >> n) & 1u) != 0u; }
    uint32_t IrqSense(uint32_t n) const { return (icr1_ >> (2u * n)) & 3u; }
    bool     IrqIsIrl(uint32_t n) const { return (icr1_ & kIcr1IrqLevel) != 0u && n < kIrlPins; }

    bool IrqRequest(uint32_t n) const {
        if (IrqSense(n) == kSenseLow) return !PinHigh(n);
        return ((irq_latch_ >> n) & 1u) != 0u;
    }

    uint32_t PintRequests() const {
        const uint32_t levels = (pin_high_ >> kPintBit0) & kPintPinMask;
        return ~(levels ^ icr2_) & pinter_ & kPintPinMask;
    }

    uint32_t IrlCode() const {
        const uint32_t irl = pin_high_ & 0xFu;
        if (!M.irls || (icr1_ & kIcr1Irlsen) == 0u) return irl;
        const uint32_t irls = (pin_high_ >> kIrlsBit0) & 0xFu;
        return irls < irl ? irls : irl;
    }

    static void Offer(Selection& best, uint32_t level, uint32_t intevt, uint32_t intevt2) {
        if (level <= best.level) return;
        best = Selection{level, intevt, intevt2};
    }

    uint32_t RowLevel(const SourceRow& row) const { return (ipr_[row.ipr] >> row.shift) & kMaxLevel; }

    void OfferRow(Selection& best, const SourceRow& row) const {
        const uint32_t level = RowLevel(row);
        Offer(best, level, row.level_coded ? LevelCode(level) : row.intevt2, row.intevt2);
    }

    bool AllMaskedLocked() const { return (icr1_ & kIcr1Mai) != 0u && !nmi_->High(); }

    Selection SelectLocked(Scope scope) const {
        Selection best;
        if (AllMaskedLocked()) return best;
        const bool     standby   = scope == Scope::kStandbyWake;
        const uint32_t irq_pins  = standby ? kStandbyWakeIrqPins : kIrqPins;
        const uint32_t modules   = standby ? kStandbyWakeModules : ~0u;
        uint32_t       first_irq = 0u;
        if ((icr1_ & kIcr1IrqLevel) != 0u) {
            first_irq          = kIrlPins;
            const uint32_t irl = IrlCode();
            if (irl != kIrlNone) {
                const uint32_t level = kMaxLevel - irl;
                Offer(best, level, LevelCode(level), LevelCode(level));
            }
        }
        for (uint32_t n = first_irq; n < irq_pins; ++n) {
            if (IrqRequest(n)) OfferRow(best, kIrqRows[n]);
        }
        const uint32_t pint = PintRequests();
        if ((pint & kPintGroupMask) != 0u) OfferRow(best, kPintRows[0]);
        if ((pint >> kPintGroupShift) != 0u) OfferRow(best, kPintRows[1]);
        const uint32_t requests = requests_ & modules;
        for (size_t i = 0; i < std::size(kModuleRows); ++i) {
            if ((requests & (1u << i)) != 0u) OfferRow(best, kModuleRows[i]);
        }
        return best;
    }

    void PublishLocked() { channel_->PublishLevel(SelectLocked(Scope::kAll).level); }

    std::mutex                      mtx_;
    Sh77xxNmiPin*                   nmi_            = nullptr;
    GuestCpuReset*                  reset_          = nullptr;
    cerfengine::ShInterruptChannel* channel_        = nullptr;
    uint16_t                        icr1_           = 0u;
    uint16_t                        icr2_           = 0u;
    uint16_t                        pinter_         = 0u;
    uint16_t                        ipr_[kIprCount] = {};
    uint32_t                        requests_       = 0u;
    uint32_t                        intevt2_        = 0u;
    uint32_t                        pin_high_       = kAllPinsHigh;
    uint32_t                        irq_latch_      = 0u;
    uint32_t                        irr0_read_ones_ = 0u;
};

}

}
