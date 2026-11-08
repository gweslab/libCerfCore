#include "usb3317_ulpi_phy.h"

#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

namespace {

constexpr uint8_t kId[4] = {0x24u, 0x04u, 0x06u, 0x00u};

constexpr uint8_t kFunctionControl      = 0x04u;
constexpr uint8_t kOtgControl           = 0x0Au;
constexpr uint8_t kInterruptRising      = 0x0Du;
constexpr uint8_t kInterruptFalling     = 0x10u;
constexpr uint8_t kHsTxBoost            = 0x31u;
constexpr uint8_t kHeadsetAudioMode     = 0x33u;
constexpr uint8_t kFunctionControlReset = 0x20u;
constexpr uint8_t kNoTriple             = 0xFFu;

constexpr uint8_t TripleRegister(uint8_t addr) {
    if (addr >= 0x04u && addr <= 0x12u) return static_cast<uint8_t>(addr - (addr - 0x04u) % 3u);
    if (addr >= 0x16u && addr <= 0x1Bu) return static_cast<uint8_t>(addr - (addr - 0x16u) % 3u);
    if (addr >= 0x1Du && addr <= 0x1Fu) return 0x1Du;
    if (addr >= 0x36u && addr <= 0x38u) return 0x36u;
    return kNoTriple;
}

constexpr bool ReadOnly(uint8_t addr) {
    return addr <= 0x03u || (addr >= 0x13u && addr <= 0x15u) || addr == 0x20u || addr == 0x21u;
}

constexpr bool SingleAddressWrite(uint8_t addr) {
    return addr == kHsTxBoost || addr == kHeadsetAudioMode;
}

}

void Usb3317UlpiPhy::PowerOn() {
    std::memset(regs_, 0, sizeof(regs_));
    std::memcpy(regs_, kId, sizeof(kId));
    regs_[kFunctionControl]  = 0x41u;
    regs_[kOtgControl]       = 0x06u;
    regs_[kInterruptRising]  = 0x1Fu;
    regs_[kInterruptFalling] = 0x1Fu;
}

bool Usb3317UlpiPhy::Read(uint8_t addr, uint8_t& value) const {
    const uint8_t reg = TripleRegister(addr);
    if (reg != kNoTriple) {
        value = regs_[reg];
        return true;
    }
    if (!ReadOnly(addr) && !SingleAddressWrite(addr)) return false;
    value = regs_[addr];
    return true;
}

bool Usb3317UlpiPhy::Write(uint8_t addr, uint8_t value) {
    const uint8_t reg = TripleRegister(addr);
    if (reg == kNoTriple) {
        if (!SingleAddressWrite(addr)) return false;
        regs_[addr] = value;
        return true;
    }
    switch (addr - reg) {
    case 1:  regs_[reg] = static_cast<uint8_t>(regs_[reg] | value);  break;
    case 2:  regs_[reg] = static_cast<uint8_t>(regs_[reg] & ~value); break;
    default: regs_[reg] = value;                                      break;
    }
    if (reg == kFunctionControl) regs_[reg] = static_cast<uint8_t>(regs_[reg] & ~kFunctionControlReset);
    return true;
}

void Usb3317UlpiPhy::SaveState(StateWriter& w) const {
    w.WriteBytes("ulpi_regs", regs_, sizeof(regs_));
}

void Usb3317UlpiPhy::RestoreState(StateReader& r) {
    r.ReadBytes("ulpi_regs", regs_, sizeof(regs_));
}

}
