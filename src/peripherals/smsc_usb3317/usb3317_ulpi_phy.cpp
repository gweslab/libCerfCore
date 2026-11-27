#include "usb3317_ulpi_phy.h"

namespace cerfcore {

namespace {

constexpr uint8_t kId[4] = {0x24u, 0x04u, 0x06u, 0x00u};

constexpr uint8_t kCarkitControl       = 0x19u;
constexpr uint8_t kCarkitControlClr    = 0x1Bu;
constexpr uint8_t kCarkitInterrupt     = 0x1Du;
constexpr uint8_t kCarkitInterruptClr  = 0x1Fu;
constexpr uint8_t kCarkitStatus        = 0x20u;
constexpr uint8_t kCarkitLatch         = 0x21u;
constexpr uint8_t kHsTxBoost           = 0x31u;
constexpr uint8_t kHeadsetAudioMode    = 0x33u;
constexpr uint8_t kVendorSet           = 0x36u;
constexpr uint8_t kVendorSetClr        = 0x38u;
constexpr uint8_t kNoSet               = 0xFFu;

constexpr uint8_t SetFirst(uint8_t addr) {
    if (addr >= kCarkitControl && addr <= kCarkitControlClr) return kCarkitControl;
    if (addr >= kCarkitInterrupt && addr <= kCarkitInterruptClr) return kCarkitInterrupt;
    if (addr >= kVendorSet && addr <= kVendorSetClr) return kVendorSet;
    return kNoSet;
}

constexpr bool SingleAddress(uint8_t addr) {
    return addr == kHsTxBoost || addr == kHeadsetAudioMode;
}

}

void Usb3317UlpiPhy::PowerOn() {
    PowerOnStandard(kId);
}

bool Usb3317UlpiPhy::ReadVendor(uint8_t addr, uint8_t& value) const {
    const uint8_t first = SetFirst(addr);
    if (first != kNoSet) return ReadSet(first, value);
    if (addr == kCarkitStatus || addr == kCarkitLatch || SingleAddress(addr)) {
        value = regs_[addr];
        return true;
    }
    return false;
}

bool Usb3317UlpiPhy::WriteVendor(uint8_t addr, uint8_t value) {
    const uint8_t first = SetFirst(addr);
    if (first != kNoSet) {
        WriteSet(first, addr, value);
        return true;
    }
    if (!SingleAddress(addr)) return false;
    regs_[addr] = value;
    return true;
}

}
