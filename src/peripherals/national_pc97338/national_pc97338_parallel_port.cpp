#include <cerfcore/peripherals/peripheral_base.h>
#include "../peripheral_dispatcher.h"
#include "national_pc97338.h"
#include "national_pc97338_board.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kParallelPort = 0x378u;
constexpr uint32_t kOffDtr = 0u;
constexpr uint32_t kOffStr = 1u;
constexpr uint32_t kOffCtr = 2u;

constexpr uint8_t kStrNotBusy   = 0x80u;
constexpr uint8_t kStrAck       = 0x40u;
constexpr uint8_t kStrErr       = 0x08u;
constexpr uint8_t kStrNoIrq     = 0x04u;
constexpr uint8_t kStrReserved  = 0x02u;
constexpr uint8_t kStrTimeOut   = 0x01u;
constexpr uint8_t kNoPeerStatus = kStrNotBusy | kStrAck | kStrErr | kStrNoIrq | kStrReserved | kStrTimeOut;

constexpr uint8_t kCtrReadOnes  = 0xE0u;
constexpr uint8_t kCtrReadable  = 0x1Fu;
constexpr uint8_t kCtrWritable  = 0x3Fu;
constexpr uint8_t kCtrIrqEnable = 0x10u;
constexpr uint8_t kCtrDirection = 0x20u;
constexpr uint8_t kCtrReset     = 0x04u;

class NationalPc97338ParallelPort : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        return emu_.TryGet<NationalPc97338Board>() != nullptr;
    }

    void OnReady() override {
        chip_      = &emu_.Get<NationalPc97338>();
        mmio_base_ = chip_->IsaIoBase() + kParallelPort;
        chip_->RegisterParallelIrqEnableProbe([this] { return (ctr_ & kCtrIrqEnable) != 0u; });
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            dtr_ = 0u;
            ctr_ = kCtrReset;
        });
    }

    uint32_t MmioBase() const override { return mmio_base_; }
    uint32_t MmioSize() const override { return 3u; }

    uint8_t ReadByte(uint32_t addr) override {
        if (!chip_->ParallelPortEnabled())
            emu_.Get<Fatal>().Die("PC97338 parallel port: read at 0x%08X while FER blocks the port", addr);
        switch (addr - mmio_base_) {
            case kOffDtr:
                if (chip_->ExtendedParallelPort() && (ctr_ & kCtrDirection) != 0u)
                    emu_.Get<Fatal>().Die("PC97338 parallel port: PD7-0 read with no peer on the connector");
                return dtr_;
            case kOffStr:
                return kNoPeerStatus;
            case kOffCtr:
                return static_cast<uint8_t>(kCtrReadOnes | (ctr_ & kCtrReadable));
            default: break;
        }
        HaltUnsupportedAccess("PC97338 parallel port ReadByte", addr, 0);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        if (!chip_->ParallelPortEnabled()) return;
        switch (addr - mmio_base_) {
            case kOffDtr: dtr_ = value; return;
            case kOffStr: return;
            case kOffCtr:
                if ((value & kCtrIrqEnable) != 0u && !chip_->ExtendedParallelPort())
                    emu_.Get<Fatal>().Die("PC97338 parallel port: CTR write 0x%02X enables IRQx in compatible SPP mode",
                                          value);
                ctr_ = static_cast<uint8_t>(value & kCtrWritable);
                return;
            default: break;
        }
        HaltUnsupportedAccess("PC97338 parallel port WriteByte", addr, value);
    }

    void SaveState(StateWriter& w) override {
        w.Write("dtr", dtr_);
        w.Write("ctr", ctr_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("dtr", dtr_);
        r.Read("ctr", ctr_);
    }

private:
    NationalPc97338* chip_      = nullptr;
    uint32_t         mmio_base_ = 0u;
    uint8_t          dtr_       = 0u;
    uint8_t          ctr_       = kCtrReset;
};

}

REGISTER_SERVICE(NationalPc97338ParallelPort);

}
