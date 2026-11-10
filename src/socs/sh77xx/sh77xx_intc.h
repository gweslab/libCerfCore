#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cstdint>

namespace cerfcore {

enum class Sh77xxIntcSource : uint8_t {
    kDei0,
    kDei1,
    kDei2,
    kDei3,
    kEri1,
    kRxi1,
    kBri1,
    kTxi1,
    kEri2,
    kRxi2,
    kBri2,
    kTxi2,
    kAdi,
    kTuni0,
    kTuni1,
    kTuni2,
    kTicpi2,
    kAti,
    kPri,
    kCui,
    kEri,
    kRxi,
    kTxi,
    kTei,
    kIti,
    kRcmi,
    kRovi,
    kCount,
};

enum class Sh77xxIntcPin : uint8_t {
    kIrq0,
    kIrq1,
    kIrq2,
    kIrq3,
    kIrq4,
    kIrq5,
    kPint0,
    kPint8  = kPint0 + 8,
    kPint15 = kPint0 + 15,
    kIrls0,
    kIrls3 = kIrls0 + 3,
    kCount,
};

class Sh77xxIntc : public Peripheral {
public:
    using Peripheral::Peripheral;

    virtual void SetSourceRequest(Sh77xxIntcSource source, bool asserted) = 0;
    virtual void SetPinLevel(Sh77xxIntcPin pin, bool high)                = 0;
    virtual bool PinLevel(Sh77xxIntcPin pin)                              = 0;
    virtual bool WakesStandby(Sh77xxIntcSource source)                    = 0;

    virtual uint16_t ReadArea7Half(uint32_t addr)                  = 0;
    virtual void     WriteArea7Half(uint32_t addr, uint16_t value) = 0;
};

}
