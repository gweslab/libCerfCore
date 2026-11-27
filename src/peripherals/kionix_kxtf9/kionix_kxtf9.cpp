#include "kionix_kxtf9.h"

#include "../../core/board_parts.h"
#include "kionix_kxtf9_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint8_t kAddr = 0x0Fu;

constexpr uint8_t kRegXoutL    = 0x06u;
constexpr uint8_t kRegZoutH    = 0x0Bu;
constexpr uint8_t kRegDcstResp = 0x0Cu;
constexpr uint8_t kRegWhoAmI   = 0x0Fu;
constexpr uint8_t kRegCtrl1    = 0x1Bu;
constexpr uint8_t kRegIntCtrl1 = 0x1Eu;
constexpr uint8_t kRegDataCtrl = 0x21u;

constexpr uint8_t kDcstResp      = 0x55u;
constexpr uint8_t kWhoAmI        = 0x01u;
constexpr uint8_t kCtrl1Reset    = 0x00u;
constexpr uint8_t kIntCtrl1Reset = 0x10u;
constexpr uint8_t kDataCtrlReset = 0x02u;

constexpr uint8_t kPc1           = 0x80u;
constexpr uint8_t kGselMask      = 0x18u;
constexpr uint8_t kGselShift     = 3u;
constexpr uint8_t kGselNone      = 0x18u;
constexpr uint8_t kCtrl1Features = 0x27u;

constexpr uint8_t kIntCtrl1Zero = 0xC3u;
constexpr uint8_t kDataCtrlZero = 0xC8u;
constexpr uint8_t kOsaMask      = 0x07u;
constexpr uint8_t kOsaFirst     = 0x01u;
constexpr uint8_t kOsaLast      = 0x06u;

constexpr uint16_t kCountsPerG2g = 1024u;
constexpr uint8_t  kOutputShift  = 4u;
constexpr uint8_t  kAxisZ        = 2u;

}

bool KionixKxtf9::ShouldRegister() { return emu_.Get<BoardParts>().Has(PartId::KionixKxtf9); }

void KionixKxtf9::OnReady() { PowerOn(); }

void KionixKxtf9::PowerOn() {
    ctrl1_           = kCtrl1Reset;
    int_ctrl1_       = kIntCtrl1Reset;
    data_ctrl_ = kDataCtrlReset;
    pointer_.Reset(0u, false);
}

bool KionixKxtf9::MatchesAddress(uint8_t slave_addr) const { return slave_addr == kAddr; }

void KionixKxtf9::TxnStart(uint8_t) { pointer_.Start(); }

bool KionixKxtf9::TxnWriteByte(uint8_t, uint8_t byte) {
    if (pointer_.Select(byte)) return true;
    if (!pointer_.Selected()) emu_.Get<Fatal>().Die("kionix kxtf9: data byte 0x%02X with no register address", byte);
    WriteRegister(static_cast<uint8_t>(pointer_.Advance()), byte);
    return true;
}

uint8_t KionixKxtf9::TxnReadByte(uint8_t) {
    pointer_.EndSelect();
    if (!pointer_.Selected()) emu_.Get<Fatal>().Die("kionix kxtf9: read with no register address");
    return ReadRegister(static_cast<uint8_t>(pointer_.Advance()));
}

uint8_t KionixKxtf9::ReadRegister(uint8_t reg) {
    if (reg >= kRegXoutL && reg <= kRegZoutH) return ReadOutput(reg);
    switch (reg) {
    case kRegDcstResp: return kDcstResp;
    case kRegWhoAmI:   return kWhoAmI;
    case kRegCtrl1:    return ctrl1_;
    case kRegIntCtrl1: return int_ctrl1_;
    case kRegDataCtrl: return data_ctrl_;
    default: break;
    }
    emu_.Get<Fatal>().Die("kionix kxtf9: read of register 0x%02X", reg);
}

uint8_t KionixKxtf9::ReadOutput(uint8_t reg) {
    if ((ctrl1_ & kPc1) == 0u) {
        emu_.Get<Fatal>().Die("kionix kxtf9: read of output register 0x%02X in stand-by, CTRL_REG1 0x%02X", reg,
                              ctrl1_);
    }
    const uint8_t  index  = static_cast<uint8_t>(reg - kRegXoutL);
    const uint16_t counts = index / 2u == kAxisZ ? Sensitivity() : 0u;
    const uint16_t word   = static_cast<uint16_t>(counts << kOutputShift);
    return static_cast<uint8_t>(index % 2u == 0u ? word : word >> 8);
}

uint16_t KionixKxtf9::Sensitivity() const {
    return static_cast<uint16_t>(kCountsPerG2g >> ((ctrl1_ & kGselMask) >> kGselShift));
}

void KionixKxtf9::WriteRegister(uint8_t reg, uint8_t value) {
    switch (reg) {
    case kRegCtrl1:
        if ((value & kCtrl1Features) != 0u || (value & kGselMask) == kGselNone) {
            emu_.Get<Fatal>().Die("kionix kxtf9: CTRL_REG1 write 0x%02X is not modeled", value);
        }
        ctrl1_ = value;
        return;
    case kRegIntCtrl1:
        if ((value & kIntCtrl1Zero) != 0u) {
            emu_.Get<Fatal>().Die("kionix kxtf9: INT_CTRL_REG1 write 0x%02X sets a reserved bit", value);
        }
        int_ctrl1_ = value;
        return;
    case kRegDataCtrl:
        if ((value & kDataCtrlZero) != 0u || (value & kOsaMask) < kOsaFirst || (value & kOsaMask) > kOsaLast) {
            emu_.Get<Fatal>().Die("kionix kxtf9: DATA_CTRL_REG write 0x%02X is not modeled", value);
        }
        data_ctrl_ = value;
        return;
    default: break;
    }
    emu_.Get<Fatal>().Die("kionix kxtf9: write of 0x%02X to register 0x%02X", value, reg);
}

void KionixKxtf9::SaveState(StateWriter& w) {
    w.Write<uint8_t>("kxtf9_ctrl1", ctrl1_);
    w.Write<uint8_t>("kxtf9_int_ctrl1", int_ctrl1_);
    w.Write<uint8_t>("kxtf9_data_ctrl", data_ctrl_);
    w.Write<uint16_t>("kxtf9_address", pointer_.Register());
    w.Write<uint8_t>("kxtf9_address_set", pointer_.Selected() ? 1u : 0u);
    w.Write<uint8_t>("kxtf9_address_pending", pointer_.Pending() ? 1u : 0u);
}

void KionixKxtf9::RestoreState(StateReader& r) {
    uint16_t address = 0u;
    uint8_t  set = 0u, pending = 0u;
    r.Read("kxtf9_ctrl1", ctrl1_);
    r.Read("kxtf9_int_ctrl1", int_ctrl1_);
    r.Read("kxtf9_data_ctrl", data_ctrl_);
    r.Read("kxtf9_address", address);
    r.Read("kxtf9_address_set", set);
    r.Read("kxtf9_address_pending", pending);
    pointer_.Restore(address, set != 0u, pending != 0u);
}

REGISTER_SERVICE(KionixKxtf9);

}
