#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include "imx6_i2c_bus.h"
#include "../i2c_slave.h"

#include <cstdint>
#include "imx6_id.h"

namespace cerfcore {

namespace cerf_imx6_i2c_detail {

template <uint32_t kBase> class Imx6I2c : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
    }

    uint32_t MmioBase() const override { return kBase; }

private:
    uint32_t MmioSize() const override { return 0x4000u; }

    uint8_t ReadByte(uint32_t addr) override {
        if ((addr & 1u) != 0u) HaltUnsupportedAccess("ReadByte", addr, 0);
        return static_cast<uint8_t>(ReadHalf(addr));
    }
    uint16_t ReadHalf(uint32_t addr) override {
        switch (addr - kBase) {
        case 0x00u: return iadr_;
        case 0x04u: return ifdr_;
        case 0x08u: return i2cr_;
        case 0x0Cu:
            i2sr_ = static_cast<uint16_t>((i2sr_ & (kIcf | kIif | kIal | kRxak)) | ((i2cr_ & kMsta) ? kIbb : 0u));
            return i2sr_;
        case 0x10u: return MasterReadData();
        }
        HaltUnsupportedAccess("ReadHalf", addr, 0);
    }
    uint32_t ReadWord(uint32_t addr) override { return ReadHalf(addr); }

    void WriteByte(uint32_t addr, uint8_t value) override {
        if ((addr & 1u) != 0u) HaltUnsupportedAccess("WriteByte", addr, value);
        WriteHalf(addr, value);
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        switch (addr - kBase) {
        case 0x00u: iadr_ = value; return;
        case 0x04u: ifdr_ = value; return;
        case 0x08u: {
            if ((value & kIien) != 0u || ((value & kMsta) != 0u && (value & kIen) == 0u))
                HaltUnsupportedAccess("imx6-i2c I2CR", addr, value);
            const uint16_t old_cr = i2cr_;
            if ((old_cr & kIen) != 0u && (value & kIen) == 0u) {
                Reset();
                return;
            }
            const bool was_master = (i2cr_ & kMsta) != 0;
            const bool is_master = (value & kMsta) != 0;
            const bool repeated = was_master && is_master && (value & kRsta) != 0;
            i2cr_ = static_cast<uint16_t>(value & ~kRsta);
            if (!was_master && is_master) {
                i2sr_ |= kIbb;
                expecting_addr_ = true;
            } else if (repeated) {
                expecting_addr_ = true;
            } else if (was_master && !is_master) {
                i2sr_ = static_cast<uint16_t>((i2sr_ & ~kIbb) | kIcf | kIif);
                expecting_addr_ = true;
                if (device_ && read_phase_) {
                    stop_pending_final_read_ = true;
                } else {
                    device_ = nullptr;
                    read_phase_ = false;
                    stop_pending_final_read_ = false;
                }
            } else if (is_master && device_ && read_phase_ && ((old_cr & kMtx) != 0) && ((value & kMtx) == 0)) {
                rx_shift_ = 0x00u;
            }
            return;
        }
        case 0x0Cu:
            i2sr_ &= (value | ~(kIif | kIal));
            return;
        case 0x10u:
            if ((i2cr_ & kMsta) == 0u && (i2cr_ & kMtx) != 0u)
                HaltUnsupportedAccess("imx6-i2c I2DR write in slave mode is not modelled", addr, value);
            i2dr_ = value;
            HandleWriteData(static_cast<uint8_t>(value));
            StartByte();
            i2sr_ &= ~kRxak;
            return;
        }
        HaltUnsupportedAccess("WriteHalf", addr, value);
    }
    void WriteWord(uint32_t addr, uint32_t value) override { WriteHalf(addr, static_cast<uint16_t>(value)); }

    void SaveState(StateWriter& w) override {
        SaveControllerState(w);
        emu_.Get<Imx6I2cBus>().SaveDevices(kBase, w);
    }
    void RestoreState(StateReader& r) override {
        RestoreControllerState(r);
        emu_.Get<Imx6I2cBus>().RestoreDevices(kBase, r);
    }

    void PostRestore() override {
        if (!expecting_addr_ || stop_pending_final_read_)
            device_ = emu_.Get<Imx6I2cBus>().Find(kBase, slave_addr_);
    }

private:
    void Reset() {
        iadr_ = 0u;
        ifdr_ = 0u;
        i2cr_ = 0u;
        i2sr_ = kIcf | kRxak;
        i2dr_ = 0u;
        slave_addr_ = 0u;
        rx_shift_ = 0u;
        expecting_addr_ = true;
        read_phase_ = false;
        stop_pending_final_read_ = false;
        device_ = nullptr;
    }

    void SaveControllerState(StateWriter& w) const {
        w.Write("iadr", iadr_);
        w.Write("ifdr", ifdr_);
        w.Write("i2cr", i2cr_);
        w.Write("i2sr", i2sr_);
        w.Write("i2dr", i2dr_);
        w.Write("slave_addr", slave_addr_);
        w.Write("rx_shift", rx_shift_);
        w.Write<uint8_t>("phase_flags", static_cast<uint8_t>((expecting_addr_ ? 1u : 0u) | (read_phase_ ? 2u : 0u) |
                                                             (stop_pending_final_read_ ? 8u : 0u)));
    }

    void RestoreControllerState(StateReader& r) {
        r.Read("iadr", iadr_);
        r.Read("ifdr", ifdr_);
        r.Read("i2cr", i2cr_);
        r.Read("i2sr", i2sr_);
        r.Read("i2dr", i2dr_);
        r.Read("slave_addr", slave_addr_);
        r.Read("rx_shift", rx_shift_);
        uint8_t flags = 0;
        r.Read("phase_flags", flags);
        expecting_addr_ = (flags & 1u) != 0u;
        read_phase_ = (flags & 2u) != 0u;
        stop_pending_final_read_ = (flags & 8u) != 0u;
        device_ = nullptr;
    }

    static constexpr uint16_t kIen = 0x80u;
    static constexpr uint16_t kIien = 0x40u;
    static constexpr uint16_t kMsta = 0x20u;
    static constexpr uint16_t kMtx = 0x10u;
    static constexpr uint16_t kIcf = 0x80u;
    static constexpr uint16_t kIbb = 0x20u;
    static constexpr uint16_t kIal = 0x10u;
    static constexpr uint16_t kIif = 0x02u;
    static constexpr uint16_t kRxak = 0x01u;
    static constexpr uint16_t kRsta = 0x04u;

    void HandleWriteData(uint8_t value) {
        if ((i2cr_ & kMtx) == 0) return;
        if (expecting_addr_) {
            slave_addr_ = static_cast<uint8_t>(value >> 1);
            read_phase_ = (value & 1u) != 0;
            device_ = emu_.Get<Imx6I2cBus>().Find(kBase, slave_addr_);
            if (!device_) FaultUnknownSlave();
            device_->TxnStart(slave_addr_);
            if (read_phase_) {
                rx_shift_ = 0x00u;
            }
            expecting_addr_ = false;
            return;
        }
        if (device_ && !read_phase_) device_->TxnWriteByte(slave_addr_, value);
    }

    uint16_t MasterReadData() {
        if (device_ && read_phase_ && ((i2cr_ & kMtx) == 0)) {
            const uint8_t out = rx_shift_;
            if (!stop_pending_final_read_) rx_shift_ = device_->TxnReadByte(slave_addr_);
            i2dr_ = out;
            if (stop_pending_final_read_) {
                i2sr_ |= kIcf | kIif;
                device_ = nullptr;
                read_phase_ = false;
                stop_pending_final_read_ = false;
            } else {
                StartByte();
            }
            return i2dr_;
        }
        i2sr_ |= kIcf;
        return i2dr_;
    }

    void StartByte() {
        i2sr_ &= ~kIcf;
        if ((i2cr_ & kMsta) != 0u) i2sr_ |= kIcf | kIif;
    }

    [[noreturn]] void FaultUnknownSlave() const {
        HaltUnsupportedAccess("imx6-i2c unknown slave", kBase + 0x10u, slave_addr_);
    }

    uint16_t iadr_ = 0, ifdr_ = 0, i2cr_ = 0, i2sr_ = 0x0081u, i2dr_ = 0;
    I2cSlave* device_ = nullptr;
    uint8_t slave_addr_ = 0, rx_shift_ = 0;
    bool expecting_addr_ = true, read_phase_ = false;
    bool stop_pending_final_read_ = false;
};

}

using cerf_imx6_i2c_detail::Imx6I2c;

}
