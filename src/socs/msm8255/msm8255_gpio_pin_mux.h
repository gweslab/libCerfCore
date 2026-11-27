#pragma once

#include <cerfcore/state/state_stream.h>
#include "msm8255_gpio_banks.h"

#include <atomic>
#include <cstdint>

namespace cerfcore {

constexpr bool Msm8255GpioBanksAddressablePins(const Msm8255GpioBank* banks,
                                               uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (banks[i].lo > banks[i].hi) return false;
        if (banks[i].hi >= kMsm8255GpioPinCount) return false;
        if (banks[i].hi - banks[i].lo + 1u > 32u) return false;
    }
    return true;
}

inline constexpr uint32_t kMsm8255PinPullMask   = 0x3u;
inline constexpr uint32_t kMsm8255PinFuncShift  = 2u;
inline constexpr uint32_t kMsm8255PinFuncMask   = 0xFu;
inline constexpr uint32_t kMsm8255PinDriveShift = 6u;

enum class Msm8255GpioMuxAccess {
    NotMine,
    PinAbsent,
    ConfigBitsAbsent,
    DriveUndefined,
    NoPinSelected,
    Served
};

template <uint32_t kSelectOff, uint32_t kConfigOff, uint32_t kBankCount>
class Msm8255GpioPinMux {
public:
    /* Linux arch/arm/mach-msm proc_comm.h PCOM_GPIO_CFG masks drvstr to four
       bits beside a two-bit pull and a four-bit func, and names eight drive
       strengths, GPIO_2MA through GPIO_16MA. */
    static constexpr uint32_t kConfigMask     = 0x3FFu;
    static constexpr uint32_t kDriveShift     = kMsm8255PinDriveShift;
    static constexpr uint32_t kDriveFieldMask = 0xFu;
    static constexpr uint32_t kDriveNamedMax  = 7u;
    static constexpr uint32_t kPullMask       = kMsm8255PinPullMask;
    static constexpr uint32_t kPullKeeper     = 0x2u;
    static constexpr uint32_t kPullUp         = 0x3u;

    explicit Msm8255GpioPinMux(const Msm8255GpioBanks<kBankCount>& banks)
        : banks_(banks) {}

    Msm8255GpioMuxAccess Write(uint32_t off, uint32_t value, uint32_t& bad) {
        if (off == kSelectOff) {
            if (!banks_.OwnsPin(value)) {
                bad = value;
                return Msm8255GpioMuxAccess::PinAbsent;
            }
            selected_.store(value, std::memory_order_release);
            selection_valid_.store(1u, std::memory_order_release);
            return Msm8255GpioMuxAccess::Served;
        }
        if (off != kConfigOff) return Msm8255GpioMuxAccess::NotMine;
        if (selection_valid_.load(std::memory_order_acquire) == 0u) {
            return Msm8255GpioMuxAccess::NoPinSelected;
        }
        return StoreConfig(selected_.load(std::memory_order_acquire), value, bad);
    }

    uint32_t PinsWithPull(uint32_t lo, uint32_t span, uint32_t pull) const {
        uint32_t mask = 0u;
        for (uint32_t i = 0; i < span; ++i) {
            if ((config_[lo + i].load(std::memory_order_acquire) & kPullMask) == pull) {
                mask |= 1u << i;
            }
        }
        return mask;
    }

    uint32_t Function(uint32_t pin) const {
        return (config_[pin].load(std::memory_order_acquire) >> kMsm8255PinFuncShift) &
               kMsm8255PinFuncMask;
    }

    Msm8255GpioMuxAccess SetPinConfig(uint32_t pin, uint32_t value, uint32_t& bad) {
        if (!banks_.OwnsPin(pin)) {
            bad = pin;
            return Msm8255GpioMuxAccess::PinAbsent;
        }
        return StoreConfig(pin, value, bad);
    }

    void Reset() {
        selected_.store(0u, std::memory_order_release);
        selection_valid_.store(0u, std::memory_order_release);
        for (uint32_t i = 0; i < kMsm8255GpioPinCount; ++i) {
            config_[i].store(0u, std::memory_order_release);
        }
    }

    void Save(StateWriter& w) const {
        w.Write<uint32_t>("selected", selected_.load(std::memory_order_acquire));
        w.Write<uint32_t>("selection_valid", selection_valid_.load(std::memory_order_acquire));
        for (uint32_t i = 0; i < kMsm8255GpioPinCount; ++i) {
            w.Write<uint32_t>("config", config_[i].load(std::memory_order_acquire));
        }
    }

    Msm8255GpioMuxAccess Restore(StateReader& r, uint32_t& bad) {
        uint32_t selected = 0;
        uint32_t valid    = 0;
        r.Read("selected", selected);
        r.Read("selection_valid", valid);
        uint32_t configs[kMsm8255GpioPinCount] = {};
        for (uint32_t i = 0; i < kMsm8255GpioPinCount; ++i) {
            r.Read("config", configs[i]);
        }
        if (valid != 0u && !banks_.OwnsPin(selected)) {
            bad = selected;
            return Msm8255GpioMuxAccess::PinAbsent;
        }
        for (uint32_t i = 0; i < kMsm8255GpioPinCount; ++i) {
            const Msm8255GpioMuxAccess ok = ValidateConfig(configs[i]);
            if (ok != Msm8255GpioMuxAccess::Served) {
                bad = configs[i];
                return ok;
            }
        }
        selected_.store(selected, std::memory_order_release);
        selection_valid_.store(valid != 0u ? 1u : 0u, std::memory_order_release);
        for (uint32_t i = 0; i < kMsm8255GpioPinCount; ++i) {
            config_[i].store(configs[i], std::memory_order_release);
        }
        return Msm8255GpioMuxAccess::Served;
    }

private:
    Msm8255GpioMuxAccess StoreConfig(uint32_t pin, uint32_t value, uint32_t& bad) {
        const Msm8255GpioMuxAccess ok = ValidateConfig(value);
        if (ok != Msm8255GpioMuxAccess::Served) {
            bad = value;
            return ok;
        }
        config_[pin].store(value, std::memory_order_release);
        return Msm8255GpioMuxAccess::Served;
    }

    static Msm8255GpioMuxAccess ValidateConfig(uint32_t value) {
        if ((value & ~kConfigMask) != 0u) {
            return Msm8255GpioMuxAccess::ConfigBitsAbsent;
        }
        if (((value >> kDriveShift) & kDriveFieldMask) > kDriveNamedMax) {
            return Msm8255GpioMuxAccess::DriveUndefined;
        }
        return Msm8255GpioMuxAccess::Served;
    }

    const Msm8255GpioBanks<kBankCount>& banks_;

    std::atomic<uint32_t> selected_{0};
    std::atomic<uint32_t> selection_valid_{0};
    std::atomic<uint32_t> config_[kMsm8255GpioPinCount] = {};
};

}
