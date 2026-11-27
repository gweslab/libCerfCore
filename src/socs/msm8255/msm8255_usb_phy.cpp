#include "msm8255_usb_phy.h"

namespace cerfcore {

namespace {

constexpr uint8_t kId[4] = {0x00u, 0x00u, 0x00u, 0x00u};

constexpr uint8_t kConfigReg1   = 0x30u;
constexpr uint8_t kConfigReg2   = 0x31u;
constexpr uint8_t kConfigReg3   = 0x32u;
constexpr uint8_t kChargerCtrl  = 0x34u;
constexpr uint8_t kDigoutCtrl   = 0x36u;

constexpr uint8_t kCalibratedDebug = 0x04u;

constexpr uint8_t kChargerDetectOff    = 1u << 0;
constexpr uint8_t kChargerBlockOff     = 1u << 1;
constexpr uint8_t kChargerDpSource     = 1u << 2;
constexpr uint8_t kChargerUlpiOff      = 1u << 3;
constexpr uint8_t kChargerComparator   = 1u << 4;
constexpr uint8_t kChargerContactSource = 1u << 5;
constexpr uint8_t kChargerStored =
    kChargerDetectOff | kChargerBlockOff | kChargerDpSource | kChargerUlpiOff | kChargerContactSource;
constexpr uint8_t kChargerBorn = kChargerDetectOff | kChargerBlockOff | kChargerDpSource | kChargerUlpiOff;

constexpr uint8_t kXcvrSelectMask = 0x03u;
constexpr uint8_t kXcvrLowSpeed   = 0x02u;
constexpr uint8_t kTermSelect     = 1u << 2;
constexpr uint8_t kOpModeMask     = 0x18u;
constexpr uint8_t kOpModeNormal   = 0x00u;
constexpr uint8_t kOpModeNoStuff  = 0x10u;
constexpr uint8_t kDpPulldown     = 1u << 1;

constexpr uint8_t kLineDp = 1u << 0;
constexpr uint8_t kLineDm = 1u << 1;

constexpr bool PlainVendorRegister(uint8_t addr) {
    return addr == kConfigReg1 || addr == kConfigReg2 || addr == kConfigReg3 ||
           addr == kDigoutCtrl;
}

}

void Msm8255UsbPhy::PowerOn() {
    PowerOnStandard(kId);
    regs_[kDebug]        = kCalibratedDebug;
    regs_[kChargerCtrl]  = kChargerBorn;
}

bool Msm8255UsbPhy::SetAttachment(Msm8255UsbAttachment attachment) {
    attachment_ = attachment;
    return SetVbus(attachment == Msm8255UsbAttachment::kDedicatedCharger ? UlpiVbus::kVbusValid
                                                                         : UlpiVbus::kBelowSessionEnd);
}

bool Msm8255UsbPhy::DetectorEnabled() const {
    return (regs_[kChargerCtrl] & (kChargerDetectOff | kChargerBlockOff | kChargerUlpiOff)) == 0u;
}

bool Msm8255UsbPhy::ContactSourceOn() const {
    return (regs_[kChargerCtrl] & (kChargerBlockOff | kChargerUlpiOff)) == 0u &&
           (regs_[kChargerCtrl] & kChargerContactSource) != 0u;
}

uint8_t Msm8255UsbPhy::LineState() const {
    const uint8_t func   = regs_[kFunctionControl];
    const uint8_t opmode = static_cast<uint8_t>(func & kOpModeMask);
    const bool pullup = (func & kTermSelect) != 0u && (regs_[kOtgControl] & kDpPulldown) == 0u &&
                        (opmode == kOpModeNormal || opmode == kOpModeNoStuff);
    const uint8_t pulled = (func & kXcvrSelectMask) == kXcvrLowSpeed ? kLineDm : kLineDp;
    if (attachment_ == Msm8255UsbAttachment::kDedicatedCharger) return pullup ? kLineDp | kLineDm : 0u;
    if (pullup) return pulled;
    return ContactSourceOn() ? kLineDp : 0u;
}

bool Msm8255UsbPhy::ReadVendor(uint8_t addr, uint8_t& value) const {
    if (addr == kChargerCtrl) {
        const bool high = DetectorEnabled() && attachment_ == Msm8255UsbAttachment::kDedicatedCharger;
        value = static_cast<uint8_t>(regs_[kChargerCtrl] | (high ? kChargerComparator : 0u));
        return true;
    }
    if (!PlainVendorRegister(addr)) return false;
    value = regs_[addr];
    return true;
}

bool Msm8255UsbPhy::WriteVendor(uint8_t addr, uint8_t value) {
    if (addr == kChargerCtrl) {
        if ((value & ~(kChargerStored | kChargerComparator)) != 0u) return false;
        regs_[kChargerCtrl] = static_cast<uint8_t>(value & kChargerStored);
        return true;
    }
    if (!PlainVendorRegister(addr)) return false;
    regs_[addr] = value;
    return true;
}

}
