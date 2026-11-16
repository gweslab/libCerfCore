#include "stm32f2_gpio.h"
#include "stm32f2_register.h"

#include <cerfcore/core/log.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace stm32f2 {

namespace {

constexpr uint32_t kModer = 0x00u;
constexpr uint32_t kOtyper = 0x04u;
constexpr uint32_t kOspeedr = 0x08u;
constexpr uint32_t kPupdr = 0x0Cu;
constexpr uint32_t kIdr = 0x10u;
constexpr uint32_t kOdr = 0x14u;
constexpr uint32_t kBsrr = 0x18u;
constexpr uint32_t kLckr = 0x1Cu;
constexpr uint32_t kAfrl = 0x20u;
constexpr uint32_t kAfrh = 0x24u;

}

Gpio::Gpio(Mcu& mcu, GpioPins& pins, uint32_t port) : mcu_(mcu), pins_(pins), port_(port) {
    if (port == 0u) {
        moder_ = 0xA8000000u;
        pupdr_ = 0x64000000u;
    } else if (port == 1u) {
        moder_ = 0x00000280u;
        ospeedr_ = 0x000000C0u;
        pupdr_ = 0x00000100u;
    }
}

uint32_t Gpio::DrivenMask() const {
    uint32_t mask = 0u;
    for (uint32_t pin = 0u; pin < 16u; ++pin) {
        if (Driven(pin)) mask |= 1u << pin;
    }
    return mask;
}

bool Gpio::DrivenLevel(uint32_t pin, bool* level) {
    if (Driven(pin)) {
        *level = ((odr_ >> pin) & 1u) != 0u;
        return true;
    }
    if (Mode(pin) != 2u || !pins_.AlternateLevel(port_, pin, AlternateFunction(pin), level)) return false;
    return !*level || ((otyper_ >> pin) & 1u) == 0u;
}

bool Gpio::PinLevel(uint32_t pin) {
    bool level = false;
    if (DrivenLevel(pin, &level)) return level;
    if (Mode(pin) == 3u) return false;
    if (pins_.ExternalLevel(port_, pin, &level)) return level;
    const uint32_t pull = (pupdr_ >> (2u * pin)) & 3u;
    if (pull == 1u) return true;
    if (pull == 2u) return false;
    constexpr bool kFloatingInputLevel = false;
    if ((floating_reported_ & (1u << pin)) == 0u) {
        floating_reported_ |= 1u << pin;
        LOG(Caution, "STM32F2 GPIO: P%c%u is a floating input with no modelled board net, read as %u\n",
            static_cast<char>('A' + port_), pin, kFloatingInputLevel ? 1u : 0u);
    }
    return kFloatingInputLevel;
}

uint32_t Gpio::InputData() {
    uint32_t value = 0u;
    for (uint32_t pin = 0u; pin < 16u; ++pin) {
        if (PinLevel(pin)) value |= 1u << pin;
    }
    return value;
}

uint32_t Gpio::Read(uint32_t offset, uint32_t size) {
    uint32_t value = 0u;
    switch (offset & ~3u) {
    case kModer: value = moder_; break;
    case kOtyper: value = otyper_; break;
    case kOspeedr: value = ospeedr_; break;
    case kPupdr: value = pupdr_; break;
    case kIdr: value = InputData(); break;
    case kOdr: value = odr_; break;
    case kBsrr: value = 0u; break;
    case kAfrl: value = afrl_; break;
    case kAfrh: value = afrh_; break;
    default: mcu_.Fault("STM32F2 GPIO: read of an unmodelled register", Gpio::kBaseA + port_ * kStride + offset);
    }
    (void)size;
    return value >> ((offset & 3u) * 8u);
}

void Gpio::Write(uint32_t offset, uint32_t size, uint32_t value) {
    const uint32_t word = offset & ~3u;
    const uint32_t old_driven = DrivenMask();
    const uint32_t old_odr = odr_;
    switch (word) {
    case kModer:
        moder_ = MergeRegisterWrite(moder_, offset, size, value);
        NotifyOutputs(old_driven, old_odr);
        return;
    case kOtyper:
        otyper_ = MergeRegisterWrite(otyper_, offset, size, value);
        if ((otyper_ & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 GPIO: reserved OTYPER bits are set", otyper_);
        NotifyOutputs(old_driven, old_odr);
        return;
    case kOspeedr: ospeedr_ = MergeRegisterWrite(ospeedr_, offset, size, value); return;
    case kPupdr: {
        const uint32_t next = MergeRegisterWrite(pupdr_, offset, size, value);
        if ((next & (next >> 1) & 0x55555555u) != 0u)
            mcu_.Fault("STM32F2 GPIO: reserved PUPDR encoding 11", next);
        pupdr_ = next;
        return;
    }
    case kOdr:
        odr_ = MergeRegisterWrite(odr_, offset, size, value);
        if ((odr_ & 0xFFFF0000u) != 0u) mcu_.Fault("STM32F2 GPIO: reserved ODR bits are set", odr_);
        NotifyOutputs(old_driven, old_odr);
        return;
    case kBsrr: {
        const uint32_t bsrr = MergeRegisterWrite(0u, offset, size, value);
        odr_ = ((odr_ & ~(bsrr >> 16)) | bsrr) & 0xFFFFu;
        NotifyOutputs(old_driven, old_odr);
        return;
    }
    case kLckr: mcu_.Fault("STM32F2 GPIO: configuration lock is not modelled", value);
    case kAfrl: afrl_ = MergeRegisterWrite(afrl_, offset, size, value); return;
    case kAfrh: afrh_ = MergeRegisterWrite(afrh_, offset, size, value); return;
    default: mcu_.Fault("STM32F2 GPIO: write of an unmodelled register", Gpio::kBaseA + port_ * kStride + offset);
    }
}

void Gpio::NotifyOutputs(uint32_t old_driven, uint32_t old_odr) {
    const uint32_t driven = DrivenMask();
    for (uint32_t pin = 0u; pin < 16u; ++pin) {
        const bool now = ((driven >> pin) & 1u) != 0u;
        const bool level = ((odr_ >> pin) & 1u) != 0u;
        if (now != (((old_driven >> pin) & 1u) != 0u) || (now && level != (((old_odr >> pin) & 1u) != 0u)))
            pins_.OutputChanged(port_, pin, now, level);
    }
}

void Gpio::SaveState(StateWriter& writer) const {
    writer.Write("gpio_moder", moder_);
    writer.Write("gpio_otyper", otyper_);
    writer.Write("gpio_ospeedr", ospeedr_);
    writer.Write("gpio_pupdr", pupdr_);
    writer.Write("gpio_odr", odr_);
    writer.Write("gpio_afrl", afrl_);
    writer.Write("gpio_afrh", afrh_);
}

void Gpio::RestoreState(StateReader& reader) {
    reader.Read("gpio_moder", moder_);
    reader.Read("gpio_otyper", otyper_);
    reader.Read("gpio_ospeedr", ospeedr_);
    reader.Read("gpio_pupdr", pupdr_);
    reader.Read("gpio_odr", odr_);
    reader.Read("gpio_afrl", afrl_);
    reader.Read("gpio_afrh", afrh_);
}

}

}
