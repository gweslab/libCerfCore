#include "akm_ak8975.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <utility>

namespace cerfcore {

namespace {

constexpr uint8_t kRegWia    = 0x00u;
constexpr uint8_t kRegInfo   = 0x01u;
constexpr uint8_t kRegSt1    = 0x02u;
constexpr uint8_t kRegHxl  = 0x03u;
constexpr uint8_t kRegHzh  = 0x08u;
constexpr uint8_t kRegSt2  = 0x09u;
constexpr uint8_t kRegCntl = 0x0Au;
constexpr uint8_t kRegAstc   = 0x0Cu;
constexpr uint8_t kRegI2cdis = 0x0Fu;
constexpr uint8_t kRegAsax   = 0x10u;
constexpr uint8_t kRegAsaz   = 0x12u;

constexpr uint8_t kDeviceId     = 0x48u;
constexpr uint8_t kInfo         = 0x00u;
constexpr uint8_t kSt2Clear     = 0x00u;
constexpr uint8_t kI2cdisCode   = 0x1Bu;
constexpr uint8_t kI2cdisClear  = 0x00u;
constexpr uint8_t kDrdy     = 1u << 0;
constexpr uint8_t kSelf     = 1u << 6;

constexpr uint8_t kModeMask      = 0x0Fu;
constexpr uint8_t kModePowerDown = 0x0u;
constexpr uint8_t kModeSingle    = 0x1u;
constexpr uint8_t kModeSelfTest  = 0x8u;
constexpr uint8_t kModeFuseRom   = 0xFu;

constexpr uint8_t kAsa = 0x80u;

constexpr int16_t kSceneX    = 0;
constexpr int16_t kSceneY    = 0;
constexpr int16_t kSceneZ    = 0;
constexpr int16_t kSelfTestX = 0;
constexpr int16_t kSelfTestY = 0;
constexpr int16_t kSelfTestZ = -650;

}

void AkmAk8975::OnReady() {
    PowerOn();
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { DriveDrdy(); });
}

void AkmAk8975::PowerOn() {
    data_.fill(0u);
    mode_            = kModePowerDown;
    astc_            = 0u;
    st1_             = 0u;
    i2c_disabled_    = false;
    pointer_.Reset(kRegWia, false);
}

bool AkmAk8975::MatchesAddress(uint8_t slave_addr) const {
    return !i2c_disabled_ && slave_addr == DeviceAddress();
}

bool AkmAk8975::OwnsAddress(uint8_t slave_addr) const { return slave_addr == DeviceAddress(); }

void AkmAk8975::TxnStart(uint8_t) { pointer_.Start(); }

bool AkmAk8975::TxnWriteByte(uint8_t, uint8_t byte) {
    if (pointer_.Select(byte)) {
        if (byte > kRegAsaz) {
            emu_.Get<Fatal>().Die("akm ak8975: register address 0x%02X", byte);
        }
        return true;
    }
    if (!pointer_.Selected()) {
        emu_.Get<Fatal>().Die("akm ak8975: data byte 0x%02X with no register address", byte);
    }
    WriteRegister(static_cast<uint8_t>(pointer_.Register()), byte);
    NextAddress();
    return true;
}

uint8_t AkmAk8975::TxnReadByte(uint8_t) {
    pointer_.EndSelect();
    if (!pointer_.Selected()) emu_.Get<Fatal>().Die("akm ak8975: read with no register address");
    const uint8_t value = ReadRegister(static_cast<uint8_t>(pointer_.Register()));
    NextAddress();
    return value;
}

void AkmAk8975::NextAddress() {
    const uint16_t reg = pointer_.Register();
    if (reg < kRegAstc || (reg >= kRegAsax && reg < kRegAsaz)) {
        pointer_.Advance();
    } else if (reg == kRegAstc) {
        pointer_.Point(kRegWia);
    } else if (reg == kRegAsaz) {
        pointer_.Point(kRegAsax);
    } else {
        pointer_.Clear();
    }
}

uint8_t AkmAk8975::ReadRegister(uint8_t reg) {
    if ((reg >= kRegHxl && reg <= kRegHzh) || reg == kRegSt2) {
        const bool ready = (st1_ & kDrdy) != 0u;
        st1_             = 0u;
        if (ready) DriveDrdy();
        return reg == kRegSt2 ? kSt2Clear : data_[reg - kRegHxl];
    }
    if (reg >= kRegAsax && reg <= kRegAsaz) {
        if (mode_ != kModeFuseRom) {
            emu_.Get<Fatal>().Die("akm ak8975: read of fuse ROM register 0x%02X in mode %u", reg, mode_);
        }
        return kAsa;
    }
    switch (reg) {
    case kRegWia:    return kDeviceId;
    case kRegInfo:   return kInfo;
    case kRegSt1:    return st1_;
    case kRegCntl:   return mode_;
    case kRegAstc:   return astc_;
    case kRegI2cdis: return kI2cdisClear;
    default: break;
    }
    emu_.Get<Fatal>().Die("akm ak8975: read of register 0x%02X", reg);
}

void AkmAk8975::WriteRegister(uint8_t reg, uint8_t value) {
    if (reg == kRegCntl) {
        WriteControl(value);
        return;
    }
    if (reg == kRegAstc) {
        if ((value & ~kSelf) != 0u) {
            emu_.Get<Fatal>().Die("akm ak8975: ASTC write 0x%02X sets a bit other than SELF", value);
        }
        astc_ = value;
        return;
    }
    if (reg == kRegI2cdis) {
        if (value != kI2cdisCode) {
            emu_.Get<Fatal>().Die("akm ak8975: I2CDIS write 0x%02X", value);
        }
        i2c_disabled_ = true;
        return;
    }
    if (reg <= kRegSt2 || (reg >= kRegAsax && reg <= kRegAsaz)) return;
    emu_.Get<Fatal>().Die("akm ak8975: write of 0x%02X to register 0x%02X", value, reg);
}

void AkmAk8975::WriteControl(uint8_t value) {
    if ((value & ~kModeMask) != 0u) {
        emu_.Get<Fatal>().Die("akm ak8975: CNTL write 0x%02X sets a bit above MODE", value);
    }
    const uint8_t mode = static_cast<uint8_t>(value & kModeMask);
    if (mode_ != kModePowerDown && mode != kModePowerDown) {
        emu_.Get<Fatal>().Die("akm ak8975: CNTL mode %u written in mode %u", mode, mode_);
    }
    data_.fill(0u);
    st1_ = 0u;
    switch (mode) {
    case kModePowerDown:
    case kModeFuseRom:
        mode_ = mode;
        break;
    case kModeSingle:
        if (astc_ != 0u) {
            emu_.Get<Fatal>().Die("akm ak8975: single measurement with ASTC 0x%02X", astc_);
        }
        Measure(kSceneX, kSceneY, kSceneZ);
        break;
    case kModeSelfTest:
        if (astc_ != kSelf) {
            emu_.Get<Fatal>().Die("akm ak8975: self-test mode with ASTC 0x%02X", astc_);
        }
        Measure(kSelfTestX, kSelfTestY, kSelfTestZ);
        break;
    default:
        emu_.Get<Fatal>().Die("akm ak8975: CNTL mode %u is not modeled", mode);
    }
    DriveDrdy();
}

void AkmAk8975::Measure(int16_t x, int16_t y, int16_t z) {
    const int16_t axes[] = {x, y, z};
    for (uint32_t axis = 0; axis < 3u; ++axis) {
        const uint16_t word    = static_cast<uint16_t>(axes[axis]);
        data_[2u * axis]       = static_cast<uint8_t>(word);
        data_[2u * axis + 1u]  = static_cast<uint8_t>(word >> 8);
    }
    st1_  = kDrdy;
    mode_ = kModePowerDown;
}

void AkmAk8975::DriveDrdy() { drdy_listeners_.Call((st1_ & kDrdy) != 0u); }

void AkmAk8975::AddDrdyListener(CallbackList<bool>::Callback listener) {
    listener((st1_ & kDrdy) != 0u);
    drdy_listeners_.Add(std::move(listener));
}

void AkmAk8975::SaveState(StateWriter& w) {
    for (uint8_t byte : data_) w.Write<uint8_t>("akm_data", byte);
    w.Write<uint8_t>("akm_mode", mode_);
    w.Write<uint8_t>("akm_astc", astc_);
    w.Write<uint8_t>("akm_st1", st1_);
    w.Write<uint8_t>("akm_address", static_cast<uint8_t>(pointer_.Register()));
    w.Write<uint8_t>("akm_address_set", pointer_.Selected() ? 1u : 0u);
    w.Write<uint8_t>("akm_address_pending", pointer_.Pending() ? 1u : 0u);
    w.Write<uint8_t>("akm_i2c_disabled", i2c_disabled_ ? 1u : 0u);
}

void AkmAk8975::RestoreState(StateReader& r) {
    for (uint8_t& byte : data_) r.Read("akm_data", byte);
    uint8_t address = 0u, set = 0u, pending = 0u, disabled = 0u;
    r.Read("akm_mode", mode_);
    r.Read("akm_astc", astc_);
    r.Read("akm_st1", st1_);
    r.Read("akm_address", address);
    r.Read("akm_address_set", set);
    r.Read("akm_address_pending", pending);
    r.Read("akm_i2c_disabled", disabled);
    pointer_.Restore(address, set != 0u, pending != 0u);
    i2c_disabled_ = disabled != 0u;
}

void AkmAk8975::PostRestore() { DriveDrdy(); }

}
