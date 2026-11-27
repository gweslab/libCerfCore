#include "ulpi_phy.h"

#include <cerfcore/state/state_stream.h>

#include <cstring>

namespace cerfcore {

namespace {

constexpr uint8_t kProductIdHigh       = 0x03u;
constexpr uint8_t kInterfaceControl    = 0x07u;
constexpr uint8_t kInterruptRising     = 0x0Du;
constexpr uint8_t kInterruptFalling    = 0x10u;
constexpr uint8_t kInterruptFallingClr = 0x12u;
constexpr uint8_t kInterruptStatus     = 0x13u;
constexpr uint8_t kInterruptLatch      = 0x14u;
constexpr uint8_t kScratch             = 0x16u;
constexpr uint8_t kScratchClr          = 0x18u;

constexpr uint8_t kFunctionControlBorn     = 0x41u;
constexpr uint8_t kFunctionControlReset    = 0x20u;
constexpr uint8_t kFunctionControlSuspendM = 0x40u;
constexpr uint8_t kInterfaceSerialModes    = 0x07u;
constexpr uint8_t kInterfaceClockSuspendM  = 0x08u;
constexpr uint8_t kOtgControlBorn          = 0x06u;
constexpr uint8_t kInterruptEnableBorn     = 0x1Fu;

constexpr bool ReadOnlyStandard(uint8_t addr) {
    return addr <= kProductIdHigh || (addr >= kInterruptStatus && addr <= UlpiPhy::kDebug);
}

constexpr bool StandardSet(uint8_t addr) {
    return addr >= UlpiPhy::kFunctionControl && addr <= kInterruptFallingClr;
}

constexpr bool ScratchSet(uint8_t addr) {
    return addr >= kScratch && addr <= kScratchClr;
}

}

void UlpiPhy::PowerOnStandard(const uint8_t (&id)[4]) {
    std::memset(regs_, 0, sizeof(regs_));
    std::memcpy(regs_, id, sizeof(id));
    regs_[kFunctionControl]  = kFunctionControlBorn;
    regs_[kOtgControl]       = kOtgControlBorn;
    regs_[kInterruptRising]  = kInterruptEnableBorn;
    regs_[kInterruptFalling] = kInterruptEnableBorn;
}

bool UlpiPhy::ReadSet(uint8_t first, uint8_t& value) const {
    value = regs_[first];
    return true;
}

void UlpiPhy::WriteSet(uint8_t first, uint8_t addr, uint8_t value) {
    switch (addr - first) {
    case 1:  regs_[first] = static_cast<uint8_t>(regs_[first] | value);  break;
    case 2:  regs_[first] = static_cast<uint8_t>(regs_[first] & ~value); break;
    default: regs_[first] = value;                                        break;
    }
}

uint8_t UlpiPhy::LineState() const { return static_cast<uint8_t>(regs_[kDebug] & kLineStateMask); }

uint8_t UlpiPhy::UtmiOutputs() const {
    switch (vbus_) {
    case UlpiVbus::kBelowSessionEnd: return kStatusSessEnd;
    case UlpiVbus::kSessionEnd:      return 0u;
    case UlpiVbus::kSessionValid:    return kStatusSessValid;
    case UlpiVbus::kVbusValid:       return kStatusVbusValid | kStatusSessValid;
    }
    return 0u;
}

bool UlpiPhy::SetVbus(UlpiVbus level) {
    const uint8_t old = UtmiOutputs();
    vbus_ = level;
    const uint8_t now     = UtmiOutputs();
    const uint8_t rising  = static_cast<uint8_t>(now & ~old & regs_[kInterruptRising]);
    const uint8_t falling = static_cast<uint8_t>(old & ~now & regs_[kInterruptFalling]);
    regs_[kInterruptLatch] = static_cast<uint8_t>(regs_[kInterruptLatch] | rising | falling);
    return (rising | falling) != 0u;
}

bool UlpiPhy::Read(uint8_t addr, uint8_t& value) {
    if (addr == kDebug) {
        value = static_cast<uint8_t>((regs_[kDebug] & ~kLineStateMask) | LineState());
        return true;
    }
    if (addr == kInterruptStatus) {
        value = UtmiOutputs();
        return true;
    }
    if (addr == kInterruptLatch) {
        value = regs_[kInterruptLatch];
        regs_[kInterruptLatch] = 0u;
        return true;
    }
    if (ReadOnlyStandard(addr)) {
        value = regs_[addr];
        return true;
    }
    if (StandardSet(addr)) {
        return ReadSet(static_cast<uint8_t>(addr - (addr - kFunctionControl) % 3u), value);
    }
    if (ScratchSet(addr)) return ReadSet(kScratch, value);
    return ReadVendor(addr, value);
}

bool UlpiPhy::Write(uint8_t addr, uint8_t value) {
    if (StandardSet(addr)) {
        const uint8_t first     = static_cast<uint8_t>(addr - (addr - kFunctionControl) % 3u);
        const bool    was_awake = Synchronous();
        WriteSet(first, addr, value);
        if (first == kFunctionControl) {
            regs_[first] = static_cast<uint8_t>(regs_[first] & ~kFunctionControlReset);
        }
        if (was_awake && !Synchronous()) regs_[kInterruptLatch] = 0u;
        return true;
    }
    if (ScratchSet(addr)) {
        WriteSet(kScratch, addr, value);
        return true;
    }
    if (ReadOnlyStandard(addr)) return false;
    return WriteVendor(addr, value);
}

bool UlpiPhy::Synchronous() const {
    return (regs_[kFunctionControl] & kFunctionControlSuspendM) != 0u &&
           (regs_[kInterfaceControl] & kInterfaceSerialModes) == 0u;
}

bool UlpiPhy::ClockRuns() const {
    if ((regs_[kFunctionControl] & kFunctionControlSuspendM) == 0u) return false;
    return (regs_[kInterfaceControl] & kInterfaceSerialModes) == 0u ||
           (regs_[kInterfaceControl] & kInterfaceClockSuspendM) != 0u;
}

void UlpiPhy::Wakeup() {
    regs_[kFunctionControl] =
        static_cast<uint8_t>(regs_[kFunctionControl] | kFunctionControlSuspendM);
    regs_[kInterfaceControl] =
        static_cast<uint8_t>(regs_[kInterfaceControl] & ~kInterfaceSerialModes);
}

void UlpiPhy::SaveState(StateWriter& w) const {
    w.WriteBytes("ulpi_regs", regs_, sizeof(regs_));
    w.Write<uint8_t>("ulpi_vbus", static_cast<uint8_t>(vbus_));
}

void UlpiPhy::RestoreState(StateReader& r) {
    r.ReadBytes("ulpi_regs", regs_, sizeof(regs_));
    uint8_t vbus = 0u;
    r.Read("ulpi_vbus", vbus);
    vbus_ = static_cast<UlpiVbus>(vbus);
}

}
