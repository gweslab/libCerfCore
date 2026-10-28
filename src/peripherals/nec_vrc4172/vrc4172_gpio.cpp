#include "vrc4172_gpio.h"
#include "vrc4172_gpio_config.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include "../peripheral_dispatcher.h"
#include "../../socs/vr41xx/vr41xx_giu.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kBase = 0x15001080u;

constexpr uint32_t kData0   = 0x00u;
constexpr uint32_t kDir0    = 0x02u;
constexpr uint32_t kInten0  = 0x04u;
constexpr uint32_t kIntst0  = 0x06u;
constexpr uint32_t kInttyp0 = 0x08u;
constexpr uint32_t kIntlv0L = 0x0Au;
constexpr uint32_t kIntlv0H = 0x0Cu;

}

REGISTER_SERVICE(Vrc4172Gpio);

bool Vrc4172Gpio::ShouldRegister() {
    return emu_.TryGet<Vrc4172GpioConfig>() != nullptr;
}
void Vrc4172Gpio::OnReady() {
    const auto& config = emu_.Get<Vrc4172GpioConfig>();
    giu_pin_ = config.GiuIntPin();
    level_   = config.InitialInputLevels();
    emu_.Get<PeripheralDispatcher>().Register(this);
    auto& reset = emu_.Get<GuestCpuReset>();
    reset.RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> lk(mtx_);
        dir_ = inten_ = intst_ = intclr_ = inttyp_ = intlv0l_ = intlv0h_ = 0;
    });
    reset.RegisterResetReleaseListener([this] {
        std::lock_guard<std::mutex> lk(mtx_);
        DriveGiuLocked();
    });
}

uint8_t Vrc4172Gpio::ReadByte(uint32_t addr) {
    const uint32_t off = addr - kBase;
    if (off & 1u) HaltUnsupportedAccess("VRC4172 GPIO ReadByte (odd offset)", addr, 0);
    std::lock_guard<std::mutex> lk(mtx_);
    return static_cast<uint8_t>(ReadReg16Locked(off) & 0xFFu);
}
uint16_t Vrc4172Gpio::ReadHalf(uint32_t addr) {
    std::lock_guard<std::mutex> lk(mtx_);
    return ReadReg16Locked(addr - kBase);
}

void Vrc4172Gpio::WriteByte(uint32_t addr, uint8_t value) {
    const uint32_t off = addr - kBase;
    if (off & 1u) HaltUnsupportedAccess("VRC4172 GPIO WriteByte (odd offset)", addr, value);
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kDir0:    dir_    = static_cast<uint16_t>((dir_    & 0xFF00u) | value); return;
        case kInttyp0: inttyp_ = static_cast<uint16_t>((inttyp_ & 0xFF00u) | value); break;
        case kInten0:  inten_  = static_cast<uint16_t>((inten_  & 0xFF00u) | value); break;
        case kIntst0:  intclr_ = static_cast<uint16_t>((intclr_ & 0xFF00u) | value); break;
        default:       HaltUnsupportedAccess("VRC4172 GPIO WriteByte", kBase + off, value);
    }
    EvaluateStatusLocked();
    DriveGiuLocked();
}
void Vrc4172Gpio::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - kBase;
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kDir0:    dir_    = value; return;
        case kInttyp0: inttyp_ = value; break;
        case kInten0:  inten_  = value; break;
        case kIntst0:  intclr_ = value; break;
        case kIntlv0L: intlv0l_ = value; break;
        case kIntlv0H: intlv0h_ = value; break;
        default:       HaltUnsupportedAccess("VRC4172 GPIO WriteHalf", kBase + off, value);
    }
    EvaluateStatusLocked();
    DriveGiuLocked();
}

uint16_t Vrc4172Gpio::ReadReg16Locked(uint32_t off) {
    switch (off) {
        case kData0:   return static_cast<uint16_t>(level_ & ~dir_);
        case kDir0:    return dir_;
        case kInten0:  return inten_;
        case kIntst0:  return intst_;
        case kInttyp0: return inttyp_;
        case kIntlv0L: return intlv0l_;
        case kIntlv0H: return intlv0h_;
        default:       HaltUnsupportedAccess("VRC4172 GPIO ReadHalf", kBase + off, 0);
    }
}

void Vrc4172Gpio::SetPinLevel(int pin, bool level) {
    if (pin < 0 || pin > 15) {
        LOG(Caution, "Vrc4172Gpio::SetPinLevel: pin %d outside modeled bank-0 "
                     "[15:0] (bank-1 pins 16-23 not modeled)\n", pin);
        CerfFatalExit(CERF_FATAL_RUNTIME_ERROR);
    }
    std::lock_guard<std::mutex> lk(mtx_);

    const uint16_t bit  = static_cast<uint16_t>(1u << pin);
    const bool     prev = (level_ & bit) != 0;
    if (level) level_ |= bit; else level_ &= ~bit;

    if ((inten_ & bit) && (inttyp_ & bit) && !(intclr_ & bit)) {
        const Polarity p = PolarityOf(pin);
        bool fired;
        if (p.both_edges)  fired = (prev != level);
        else if (p.high)   fired = (!prev && level);
        else               fired = (prev && !level);
        if (fired) intst_ |= bit;
    }
    EvaluateStatusLocked();
    DriveGiuLocked();
}

void Vrc4172Gpio::EvaluateStatusLocked() {
    for (int pin = 0; pin < 16; ++pin) {
        const uint16_t bit = static_cast<uint16_t>(1u << pin);
        if (inttyp_ & bit) {
            if (intclr_ & bit) intst_ &= static_cast<uint16_t>(~bit);
            continue;
        }
        const bool active = ((level_ & bit) != 0) == PolarityOf(pin).high;
        if (!active) {
            intst_ &= static_cast<uint16_t>(~bit);
        } else if (inten_ & bit) {
            intst_ |= bit;
        }
    }
}

Vrc4172Gpio::Polarity Vrc4172Gpio::PolarityOf(int pin) const {
    const uint16_t intlv = (pin < 8) ? intlv0l_ : intlv0h_;
    const uint16_t m2    = static_cast<uint16_t>(1u << (pin & 7));
    return {(intlv & m2) != 0, (intlv & (m2 << 8)) != 0};
}

void Vrc4172Gpio::DriveGiuLocked() {
    const bool asserted = (intst_ & inten_) != 0;
    if (asserted == giu_asserted_) return;
    giu_asserted_ = asserted;
    emu_.Get<Vr41xxGiu>().SetPinLevel(giu_pin_, asserted);
}

void Vrc4172Gpio::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("dir", dir_); w.Write("inten", inten_); w.Write("intst", intst_); w.Write("intclr", intclr_);
    w.Write("inttyp", inttyp_);
    w.Write("intlv0l", intlv0l_); w.Write("intlv0h", intlv0h_); w.Write("level", level_);
}
void Vrc4172Gpio::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("dir", dir_); r.Read("inten", inten_); r.Read("intst", intst_); r.Read("intclr", intclr_);
    r.Read("inttyp", inttyp_);
    r.Read("intlv0l", intlv0l_); r.Read("intlv0h", intlv0h_); r.Read("level", level_);
}
void Vrc4172Gpio::PostRestore() {
    std::lock_guard<std::mutex> lk(mtx_);
    giu_asserted_ = (intst_ & inten_) != 0;
    emu_.Get<Vr41xxGiu>().SetPinLevel(giu_pin_, giu_asserted_);
}

}
