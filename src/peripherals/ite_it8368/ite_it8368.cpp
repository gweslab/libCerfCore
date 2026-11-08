#include "ite_it8368.h"

#include <cerfcore/boards/board_context.h>
#include "../../boards/philips_nino/philips_nino_family_id.h"
#include "../../boards/philips_velo_1/philips_velo_1_family_id.h"
#include "../../boards/sharp_mobilon_hc/sharp_mobilon_hc_family_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../pcmcia/pcmcia_slot.h"

namespace cerfcore {

namespace {

/* it8368reg.h IT8368_*_REG. */
constexpr uint32_t kRegGpioDataOut    = 0x00;
constexpr uint32_t kRegMfioDataOut    = 0x02;
constexpr uint32_t kRegGpioDir        = 0x04;
constexpr uint32_t kRegMfioDir        = 0x06;
/* it8368reg.h defines no register at 0x08 (gap between IT8368_MFIODIR_REG
   0x06 and IT8368_MFIOSEL_REG 0x0a). */
constexpr uint32_t kRegReserved08     = 0x08;
constexpr uint32_t kRegMfioSel        = 0x0A;
constexpr uint32_t kRegGpioDataIn     = 0x0C;
constexpr uint32_t kRegMfioDataIn     = 0x0E;
constexpr uint32_t kRegGpioPosIntEn   = 0x10;
constexpr uint32_t kRegMfioPosIntEn   = 0x12;
constexpr uint32_t kRegGpioNegIntEn   = 0x14;
constexpr uint32_t kRegMfioNegIntEn   = 0x16;
constexpr uint32_t kRegGpioPosIntStat = 0x18;
constexpr uint32_t kRegMfioPosIntStat = 0x1A;
constexpr uint32_t kRegGpioNegIntStat = 0x1C;
constexpr uint32_t kRegMfioNegIntStat = 0x1E;
constexpr uint32_t kRegCtrl           = 0x20;

/* it8368reg.h IT8368_GPIODATAIN_MASK - twelve GPIOs, bits 12:0. */
constexpr uint16_t kGpioMask = 0x1FFF;

/* it8368reg.h IT8368_MFIO*_MASK - eleven MFIO pins, bits 10:0. */
constexpr uint16_t kMfioMask = 0x07FF;

/* it8368reg.h IT8368_MFIOSEL_VGAEN. */
constexpr uint16_t kMfioSelVgaEn = 0x0800;

/* it8368reg.h IT8368_PIN_*. */
constexpr uint16_t kPinCrdSw     = 0x1000;
constexpr uint16_t kPinCrdDet2   = 0x0800;
constexpr uint16_t kPinCrdDet1   = 0x0400;
constexpr uint16_t kPinCrdVccOn1 = 0x0080;
constexpr uint16_t kPinCrdVccOn0 = 0x0040;
constexpr uint16_t kPinCrdVppOn1 = 0x0020;
constexpr uint16_t kPinCrdVppOn0 = 0x0010;
constexpr uint16_t kPinBcrdWp    = 0x0008;
constexpr uint16_t kPinBcrdRdy   = 0x0004;
constexpr uint16_t kPinBcrdRst   = 0x0001;

constexpr uint16_t kPinCrdDetMask = kPinCrdDet1 | kPinCrdDet2;

constexpr uint16_t kPinCrdVccMask = kPinCrdVccOn1 | kPinCrdVccOn0;
constexpr uint16_t kPinCrdVppMask = kPinCrdVppOn1 | kPinCrdVppOn0;

constexpr uint16_t kDrivablePins = kPinCrdVccMask | kPinCrdVppMask | kPinBcrdRst;

/* CRDSW is an output-capable pin the Velo drives high (nk.exe sub_9F40F688);
   NetBSD never uses it and no ROM path reads it back, so it is accepted as a
   driven output but has no realized effect (ApplyOutputsLocked models only the
   kDrivablePins set). */
constexpr uint16_t kOutputCapablePins = kDrivablePins | kPinCrdSw;

/* it8368reg.h IT8368_CTRL_*. */
constexpr uint16_t kCtrlFixAttrIo = 0x8000;
constexpr uint16_t kCtrlAddrSel   = 0x0010;
constexpr uint16_t kCtrlByteSwap  = 0x0008;
constexpr uint16_t kCtrlCardEn    = 0x0004;
constexpr uint16_t kCtrlGlobalEn  = 0x0002;
constexpr uint16_t kCtrlIntTriEn  = 0x0001;

constexpr uint16_t kCtrlSoftReset = 0x0040;

constexpr uint16_t kCtrlKnown = kCtrlFixAttrIo | kCtrlAddrSel | kCtrlByteSwap |
                                kCtrlCardEn | kCtrlGlobalEn | kCtrlIntTriEn;

}  /* namespace */

bool IteIt8368::ShouldRegister() {
    const std::string_view board = emu_.Get<BoardContext>().GetBoardFamilyId();
    return board == BoardFamilyId::PhilipsNino || board == BoardFamilyId::PhilipsVelo1 ||
           board == BoardFamilyId::SharpMobilonHc;
}

void IteIt8368::OnReady() {
    emu_.Get<Pr31x00CardSpace>().ProvideCardBuffer(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { HardwareReset(); });
}

bool IteIt8368::CardInterfaceEnabled() const { return (ctrl_ & kCtrlCardEn) != 0; }

bool IteIt8368::FixedAttributeIo() const { return true; }

uint16_t IteIt8368::GpioDataInLocked() const {
    const uint16_t driven   = static_cast<uint16_t>(gpio_dataout_ & gpio_dir_);
    const uint16_t undriven = static_cast<uint16_t>(kGpioMask & ~gpio_dir_);
    uint16_t in = static_cast<uint16_t>(driven | undriven);

    if (!slot_->HasCard()) return in;
    in &= static_cast<uint16_t>(~kPinCrdDetMask);

    if (!slot_->IsPowered()) return in;

    in &= static_cast<uint16_t>(~kPinBcrdWp);

    if (ready_ireq_low_) in &= static_cast<uint16_t>(~kPinBcrdRdy);
    else           in |= kPinBcrdRdy;
    return in;
}

void IteIt8368::LatchInputEdgesLocked() {
    const uint16_t now  = GpioDataInLocked();
    const uint16_t diff = static_cast<uint16_t>(now ^ prev_datain_);
    gpio_posintstat_ |= static_cast<uint16_t>(diff & now);
    gpio_negintstat_ |= static_cast<uint16_t>(diff & ~now);
    prev_datain_ = now;
    UpdateIntLocked();
}

void IteIt8368::SetReadyIreqLow(bool low) {
    std::lock_guard<std::mutex> lk(mu_);
    ready_ireq_low_ = low;
    LatchInputEdgesLocked();
}

void IteIt8368::NotifyCardDetect() {
    std::lock_guard<std::mutex> lk(mu_);
    LatchInputEdgesLocked();
}

bool IteIt8368::IntLevelLocked() const {
    const bool driving = (ctrl_ & kCtrlGlobalEn) != 0u;
    const bool pending = ((gpio_posintstat_ & gpio_posinten_) |
                          (gpio_negintstat_ & gpio_neginten_)) != 0u;
    return driving && pending;
}

void IteIt8368::UpdateIntLocked() {
    const bool level = IntLevelLocked();
    if (level == int_asserted_) return;

    if (!int_sink_) {
        emu_.Get<Fatal>().Die("IteIt8368: INT pin has no board wiring");
    }
    int_asserted_ = level;
    int_sink_->OnIt8368IntLevel(level);
}

IteIt8368::BusOps IteIt8368::ApplyOutputsLocked() {
    const BusOps ops = OutputOpsLocked();
    LatchInputEdgesLocked();
    return ops;
}

IteIt8368::BusOps IteIt8368::OutputOpsLocked() {
    const uint16_t driven = static_cast<uint16_t>(gpio_dataout_ & gpio_dir_);

    if (driven & kPinCrdVppOn1) {
        emu_.Get<Fatal>().Die("IteIt8368: CRDVPPON1 (Vpp 12 V) unmodeled");
    }
    if ((driven & kPinCrdVccMask) == kPinCrdVccMask) {
        emu_.Get<Fatal>().Die("IteIt8368: CRDVCCON1 and CRDVCCON0 both driven");
    }

    BusOps ops;
    ops.drove_pins = true;
    ops.power_on   = (driven & kPinCrdVccMask) != 0u;

    ops.reset_asserted = (driven & kPinBcrdRst) != 0u;
    return ops;
}

IteIt8368::BusOps IteIt8368::SoftResetLocked() {
    ClearRegistersLocked();
    return ApplyOutputsLocked();
}

void IteIt8368::ClearRegistersLocked() {
    gpio_dataout_    = 0;
    gpio_dir_        = 0;
    gpio_posinten_   = 0;
    gpio_neginten_   = 0;
    gpio_posintstat_ = 0;
    gpio_negintstat_ = 0;
    mfio_posintstat_ = 0;
    mfio_negintstat_ = 0;
    mfio_dataout_    = 0;
    mfio_dir_        = 0;
    mfio_sel_        = 0;
    ctrl_            = 0;
}

void IteIt8368::ApplyBusOps(const BusOps& ops) {
    slot_->SetPowered(ops.power_on);
    slot_->SetResetLine(ops.reset_asserted);
}

uint16_t IteIt8368::ReadReg(uint32_t off) {
    std::lock_guard<std::mutex> lk(mu_);
    switch (off) {
        case kRegGpioDataOut: return gpio_dataout_;
        case kRegGpioDir:     return gpio_dir_;

        /* nk.exe sub_9F40F39C reads MFIODATAOUT to save/restore bit 10 around the
           debug-probe strobe; the output register reads back its latch. */
        case kRegMfioDataOut: return mfio_dataout_;

        case kRegMfioDir: return mfio_dir_;

        case kRegGpioDataIn:
            LatchInputEdgesLocked();
            return GpioDataInLocked();

        case kRegGpioPosIntStat:
            LatchInputEdgesLocked();
            return gpio_posintstat_;
        case kRegGpioNegIntStat:
            LatchInputEdgesLocked();
            return gpio_negintstat_;

        case kRegGpioPosIntEn: return gpio_posinten_;
        case kRegGpioNegIntEn: return gpio_neginten_;

        case kRegCtrl: return ctrl_;

        /* pcmcia.dll suspend save sub_14912D4 reads regs 0x00..0x20; restore
           sub_149131C writes back only offsets 4/6/0x10-0x1E/0x20. */
        case kRegMfioSel:        return mfio_sel_;
        case kRegReserved08:     return 0;

        case kRegMfioDataIn:
            return static_cast<uint16_t>(mfio_dataout_ & mfio_dir_);

        /* it8368reg.h IT8368_MFIOPOSINTSTAT_REG 0x1a / IT8368_MFIONEGINTSTAT_REG
           0x1e (W1C). */
        case kRegMfioPosIntStat: return mfio_posintstat_;
        case kRegMfioNegIntStat: return mfio_negintstat_;

        case kRegMfioPosIntEn:
        case kRegMfioNegIntEn:   return 0;

        default:
            emu_.Get<Fatal>().Die("IteIt8368::ReadReg unmodeled register $%02X", off);
    }
}

void IteIt8368::WriteReg(uint32_t off, uint16_t value) {
    BusOps ops;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ops = WriteRegLocked(off, value);
    }
    FinishBusOps(ops);
}

void IteIt8368::HardwareReset() {
    BusOps ops;
    {
        std::lock_guard<std::mutex> lk(mu_);
        ClearRegistersLocked();
        ops = OutputOpsLocked();
    }
    ApplyBusOps(ops);
    std::lock_guard<std::mutex> lk(mu_);
    gpio_posintstat_ = 0;
    gpio_negintstat_ = 0;
    mfio_posintstat_ = 0;
    mfio_negintstat_ = 0;
    prev_datain_     = kPowerOnDataIn;
    LatchInputEdgesLocked();
}

void IteIt8368::FinishBusOps(const BusOps& ops) {
    if (!ops.drove_pins) return;

    ApplyBusOps(ops);

    std::lock_guard<std::mutex> lk(mu_);
    LatchInputEdgesLocked();
}

IteIt8368::BusOps IteIt8368::WriteRegLocked(uint32_t off, uint16_t value) {
    switch (off) {
        case kRegGpioDataOut:
            if (value & ~kOutputCapablePins) {
                emu_.Get<Fatal>().Die("IteIt8368: GPIODATAOUT 0x%04X drives an input "
                                      "pin", value);
            }
            gpio_dataout_ = value;
            return ApplyOutputsLocked();

        case kRegGpioDir:
            if (value & ~kOutputCapablePins) {
                emu_.Get<Fatal>().Die("IteIt8368: GPIODIR 0x%04X enables an output on a "
                                      "card status pin", value);
            }
            gpio_dir_ = value;
            return ApplyOutputsLocked();

        case kRegMfioSel:
            if (value & ~static_cast<uint16_t>(kMfioMask | kMfioSelVgaEn)) {
                emu_.Get<Fatal>().Die("IteIt8368: MFIOSEL 0x%04X sets a reserved bit",
                                      value);
            }
            if (value & kMfioSelVgaEn) {
                emu_.Get<Fatal>().Die("IteIt8368: MFIOSEL 0x%04X enables VGA", value);
            }
            mfio_sel_ = value;
            return {};

        case kRegGpioPosIntEn:
            if (value & ~kGpioMask) {
                emu_.Get<Fatal>().Die("IteIt8368: GPIOPOSINTEN 0x%04X enables a pin the "
                                      "chip does not have", value);
            }
            gpio_posinten_ = value;
            UpdateIntLocked();
            return {};

        case kRegGpioNegIntEn:
            if (value & ~kGpioMask) {
                emu_.Get<Fatal>().Die("IteIt8368: GPIONEGINTEN 0x%04X enables a pin the "
                                      "chip does not have", value);
            }
            gpio_neginten_ = value;
            UpdateIntLocked();
            return {};

        case kRegMfioPosIntEn:
        case kRegMfioNegIntEn:
            if (value != 0u) {
                emu_.Get<Fatal>().Die("IteIt8368: MFIO interrupt enable $%02X = 0x%04X on a "
                                      "pin held at its reset function", off, value);
            }
            return {};

        case kRegGpioPosIntStat:
            gpio_posintstat_ &= static_cast<uint16_t>(~value);
            UpdateIntLocked();
            return {};
        case kRegGpioNegIntStat:
            gpio_negintstat_ &= static_cast<uint16_t>(~value);
            UpdateIntLocked();
            return {};

        case kRegMfioPosIntStat:
            mfio_posintstat_ &= static_cast<uint16_t>(~value);
            return {};
        case kRegMfioNegIntStat:
            mfio_negintstat_ &= static_cast<uint16_t>(~value);
            return {};

        case kRegCtrl:
            if (value & kCtrlSoftReset) {
                if (value != kCtrlSoftReset) {
                    emu_.Get<Fatal>().Die("IteIt8368: CTRL 0x%04X combines soft reset with "
                                          "other bits", value);
                }
                return SoftResetLocked();
            }
            if (value & ~kCtrlKnown) {
                emu_.Get<Fatal>().Die("IteIt8368: CTRL 0x%04X sets a reserved bit", value);
            }
            if (value & kCtrlAddrSel) {
                emu_.Get<Fatal>().Die("IteIt8368: CTRL ADDRSEL unmodeled");
            }
            ctrl_ = value;
            UpdateIntLocked();
            return {};

        case kRegGpioDataIn:
        case kRegMfioDataIn:
            emu_.Get<Fatal>().Die("IteIt8368: write to input register $%02X", off);

        case kRegMfioDataOut:
            if (value & ~kMfioMask) {
                emu_.Get<Fatal>().Die("IteIt8368: MFIODATAOUT 0x%04X sets a reserved "
                                      "bit", value);
            }
            mfio_dataout_ = value;
            return {};

        case kRegMfioDir:
            if (value & ~kMfioMask) {
                emu_.Get<Fatal>().Die("IteIt8368: MFIODIR 0x%04X sets a reserved bit",
                                      value);
            }
            mfio_dir_ = value;
            return {};

        default:
            emu_.Get<Fatal>().Die("IteIt8368::WriteReg unmodeled register $%02X = 0x%04X",
                                  off, value);
    }
}

void IteIt8368::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mu_);
    w.Write("gpio_dataout", gpio_dataout_);
    w.Write("gpio_dir", gpio_dir_);
    w.Write("gpio_posinten", gpio_posinten_);
    w.Write("gpio_neginten", gpio_neginten_);
    w.Write("gpio_posintstat", gpio_posintstat_);
    w.Write("gpio_negintstat", gpio_negintstat_);
    w.Write("mfio_posintstat", mfio_posintstat_);
    w.Write("mfio_negintstat", mfio_negintstat_);
    w.Write("mfio_dataout", mfio_dataout_);
    w.Write("mfio_dir", mfio_dir_);
    w.Write("mfio_sel", mfio_sel_);
    w.Write("ctrl", ctrl_);
    w.Write("prev_datain", prev_datain_);
    w.Write<uint8_t>("ready_ireq_low", ready_ireq_low_ ? 1u : 0u);
}

void IteIt8368::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mu_);
    r.Read("gpio_dataout", gpio_dataout_);
    r.Read("gpio_dir", gpio_dir_);
    r.Read("gpio_posinten", gpio_posinten_);
    r.Read("gpio_neginten", gpio_neginten_);
    r.Read("gpio_posintstat", gpio_posintstat_);
    r.Read("gpio_negintstat", gpio_negintstat_);
    r.Read("mfio_posintstat", mfio_posintstat_);
    r.Read("mfio_negintstat", mfio_negintstat_);
    r.Read("mfio_dataout", mfio_dataout_);
    r.Read("mfio_dir", mfio_dir_);
    r.Read("mfio_sel", mfio_sel_);
    r.Read("ctrl", ctrl_);
    r.Read("prev_datain", prev_datain_);
    uint8_t ready_ireq_low = 0;
    r.Read("ready_ireq_low", ready_ireq_low);
    ready_ireq_low_ = ready_ireq_low != 0u;

    /* Every peripheral downstream of the INT pin restores its own copy of the
       line - the pin level in Pr31x00Io's mfio_din_, the latched edge in the
       INTC's status - and restore order across peripherals is unspecified. */
    int_asserted_ = IntLevelLocked();
}

REGISTER_SERVICE(IteIt8368);

}
