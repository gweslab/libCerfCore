#pragma once

#include "vr41xx_dsiu.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/engine_bridge.h"
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfengine/mips/mips_interrupt_channel.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "../../peripherals/serial/uart_frame.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../../tracing/kernel_debug_sink.h"
#include "../uart_endpoint.h"
#include "vr41xx_giu.h"
#include "vr41xx_icu.h"

#include <array>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace cerfcore {

namespace cerf_vr41xx_dsiu_detail {

/* VR4121 UM Table 23-1 == VR4102 UM Table 22-1. */
constexpr uint32_t kOffPort   = 0x00u;   /* PORTREG      (VR4121 23.2.1,  VR4102 22.2.1)  */
constexpr uint32_t kOffModem  = 0x02u;
constexpr uint32_t kOffAsim00 = 0x04u;   /* ASIM00REG    (VR4121 23.2.3,  VR4102 22.2.3)  */
constexpr uint32_t kOffAsim01 = 0x06u;   /* ASIM01REG    (VR4121 23.2.4,  VR4102 22.2.4)  */
constexpr uint32_t kOffRxb0R  = 0x08u;
constexpr uint32_t kOffRxb0L  = 0x0Au;
constexpr uint32_t kOffTxs0R  = 0x0Cu;
constexpr uint32_t kOffTxs0L  = 0x0Eu;   /* TXS0LREG     (VR4121 23.2.8,  VR4102 22.2.8)  */
constexpr uint32_t kOffAsis0  = 0x10u;   /* ASIS0REG     (VR4121 23.2.9,  VR4102 22.2.9)  */
constexpr uint32_t kOffIntr0  = 0x12u;   /* INTR0REG     (VR4121 23.2.10, VR4102 22.2.10) */
constexpr uint32_t kOffBprm0  = 0x16u;   /* BPRM0REG     (VR4121 23.2.11, VR4102 22.2.11) */
constexpr uint32_t kOffReset  = 0x18u;   /* DSIURESETREG (VR4121 23.2.12, VR4102 22.2.12) */

/* PORTREG D3 CDDIN, D2 CDDOUT, D1 CDRTS, D0 CDCTS switch each DSIU pin to a
   general-purpose output; D15:4 RFU, "Write 0 to these bits. 0 is returned after a read"
   (VR4121 UM 23.2.1, VR4102 UM 22.2.1). */
constexpr uint16_t kPortWritable = 0x000Fu;
constexpr uint16_t kPortCdDin    = 0x0008u;
constexpr uint16_t kPortCdDout   = 0x0004u;
constexpr uint16_t kPortCdRts    = 0x0002u;
constexpr uint16_t kPortCdCts    = 0x0001u;
constexpr uint16_t kAsim00Rxe0   = 0x0040u;

constexpr int kGpioDdout = 44;
constexpr int kGpioDdin  = 45;
constexpr int kGpioDrts  = 46;
constexpr int kGpioDcts  = 47;

/* ASIM00REG D6 RXE0 reception enable, D5:4 PS0(1:0) parity, D3 CL0 character length,
   D2 SL0 stop bits are R/W; D7 is an RFU that reads back 1 ("Write 1 to this bit. 1 is
   returned after a read"); D15:8 and D1:0 RFU read 0. Both reset rows carry D7
   (VR4121 UM 23.2.3, VR4102 UM 22.2.3). */
constexpr uint16_t kAsim00Writable  = 0x007Cu;
constexpr uint16_t kAsim00FixedRead = 0x0080u;

/* ASIM01REG D0 EBS0 "Extended bit operation enable"; D15:1 RFU read 0; both reset rows 0
   (VR4121 UM 23.2.4, VR4102 UM 22.2.4). */
constexpr uint16_t kAsim01Writable = 0x0001u;

/* TXS0LREG D7:0 "Transmit data"; D15:8 RFU read 0; both reset rows all 1
   (VR4121 UM 23.2.8, VR4102 UM 22.2.8). */
constexpr uint16_t kTxs0LData    = 0x00FFu;
constexpr uint16_t kTxs0RData    = 0x01FFu;
constexpr uint16_t kTxs0PowerOn  = 0x01FFu;
constexpr uint16_t kTxs08        = 0x0100u;
constexpr uint16_t kRxb0LData    = 0x00FFu;
constexpr uint16_t kAsis0Pe0     = 0x0004u;
constexpr uint16_t kAsis0Fe0     = 0x0002u;
constexpr uint16_t kAsis0Ove0    = 0x0001u;
constexpr uint16_t kAsim00Ps     = 0x0030u;
constexpr unsigned kAsim00PsShift = 4u;
constexpr uint16_t kAsim00Cl0    = 0x0008u;
constexpr uint16_t kAsim00Sl0    = 0x0004u;
constexpr uint16_t kAsim01Ebs0   = 0x0001u;
constexpr uint16_t kBprm0Brce0   = 0x0080u;
constexpr uint16_t kBprm0Bpr     = 0x0007u;

using DsiuParity = SerialLine::LineConfig::Parity;
constexpr std::array<uint32_t, 8> kBaud = {1200u, 2400u, 4800u, 9600u,
                                           19200u, 38400u, 57600u, 115200u};
constexpr std::array<DsiuParity, 4> kTxParity = {DsiuParity::None, DsiuParity::Space,
                                                 DsiuParity::Odd, DsiuParity::Even};
constexpr std::array<DsiuParity, 4> kRxParity = {DsiuParity::None, DsiuParity::None,
                                                 DsiuParity::Odd, DsiuParity::Even};

/* INTR0REG D3 INTDCD, D2 INTSER0, D1 INTSR0, D0 INTST0, each "Cleared to 0 when 1 is
   written"; D15:4 RFU read 0; both reset rows 0 (VR4121 UM 23.2.10, VR4102 UM 22.2.10). */
constexpr uint16_t kIntSt0    = 0x0001u;   /* "Debug serial transmit complete interrupt" */
constexpr uint16_t kIntSr0    = 0x0002u;
constexpr uint16_t kIntSer0   = 0x0004u;
constexpr uint16_t kIntCauses = 0x000Fu;

/* The ICU's DSIUINTREG (0x0B00008A) carries the same four causes in D11:8 - D11 INTDCTS,
   D10 INTSER0, D9 INTSR0, D8 INTST0 - and MDSIUINTREG (0x0B000096) enables them in D11:8
   (VR4111 UM 15.2.6 p335 + 15.2.12 p342, VR4121 UM 15.2.6 + 15.2.12, VR4102 UM 14.2.6), so
   INTR0REG's D3:0 shift by 8 into the ICU's positions. */
constexpr int kIcuCauseShift = 8;

/* BPRM0REG D7 BRCE0 baud-rate-generator count enable and D2:0 BPR0(2:0) baud rate are R/W;
   D15:8 and D6:3 are "Write 0 when writing. 0 is returned after a read"; both reset rows 0
   (VR4121 UM 23.2.11, VR4102 UM 22.2.11). */
constexpr uint16_t kBprmWritable = 0x0087u;

/* DSIURESETREG D0 DSIURST "DSIU reset. Cleared to 0 when 1 is written. 1: Reset /
   0: Normal"; D15:1 RFU read 0 (VR4121 UM 23.2.12, VR4102 UM 22.2.12). */
constexpr uint16_t kDsiuRst = 0x0001u;

struct Vr41xxDsiuModel {
    uint32_t base;
    uint32_t size;
    bool     portreg_retained_on_reset;   /* PORTREG After-reset row */
};

template <const std::string_view& Soc, const Vr41xxDsiuModel& M>
class Vr41xxDsiuBase : public Vr41xxDsiu {
public:
    using Vr41xxDsiu::Vr41xxDsiu;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == Soc;
    }

    void OnReady() override {
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                hibernating_ = false;
                ApplyResetLocked(kind);
            }
            giu_->NotifyOutputDriveChanged();
        });
        channel_ = &emu_.Get<EngineBridge>().Engine().template Get<cerfengine::MipsInterruptChannel>();
        channel_->RegisterSuspendListener([this] {
            SetPowerDown([this] { suspended_ = channel_->Suspended(); });
        });
        emu_.Get<GuestDeepSleep>().RegisterSleepEntryListener([this] {
            SetPowerDown([this] { hibernating_ = true; });
        });
        auto& giu = emu_.Get<Vr41xxGiu>();
        giu_ = &giu;
        giu.RegisterAlternateFunction(kGpioDdout, kGpioDdout, [this](int) {
            return PortPinDrive(kPortCdDout, PinDrive::kHigh);
        });
        giu.RegisterAlternateFunction(kGpioDdin, kGpioDdin, [this](int) {
            return PortPinDrive(kPortCdDin, PinDrive::kUndriven);
        });
        giu.RegisterAlternateFunction(kGpioDrts, kGpioDrts, [this](int) { return DrtsDrive(); });
        giu.RegisterAlternateFunction(kGpioDcts, kGpioDcts, [this](int) {
            return PortPinDrive(kPortCdCts, PinDrive::kUndriven);
        });
    }

    void AttachEndpoint(UartEndpoint* endpoint) override { endpoint_ = endpoint; }

    SerialLine::LineConfig TransmitFormat() override {
        std::lock_guard<std::mutex> lk(mtx_);
        return FormatLocked(kTxParity);
    }

    void ReceiveFromLine(uint8_t data, const SerialLine::LineConfig& sent) override {
        std::lock_guard<std::mutex> lk(mtx_);
        if ((asim00_ & kAsim00Rxe0) == 0u || (bprm0_ & kBprm0Brce0) == 0u) return;
        if (portreg_ & kPortCdDin) return;
        if (NineBitLocked()) {
            HaltUnsupportedAccess("VR41xx DSIU line reception in extended-bit mode",
                                  M.base + kOffAsim01, asim01_);
        }
        if (asim00_ & kAsim00Sl0) {
            HaltUnsupportedAccess("VR41xx DSIU line reception with two stop bits",
                                  M.base + kOffAsim00, asim00_);
        }
        const SerialLine::LineConfig own = FormatLocked(kRxParity);
        if (sent.baud != own.baud) {
            emu_.Get<Fatal>().Die("VR41xx DSIU: line device sends at %u bps, receiver at %u bps",
                                  sent.baud, own.baud);
        }
        const UartDecodedFrame frame = DecodeUartFrame(sent, data, own);
        asis0_errors_ = static_cast<uint16_t>((frame.parity_error ? kAsis0Pe0 : 0u) |
                                              (frame.framing_error ? kAsis0Fe0 : 0u) |
                                              (rx_unread_ ? kAsis0Ove0 : 0u));
        rxb0_      = frame.data;
        rx_unread_ = true;
        intr0_ |= kIntSr0;
        if (asis0_errors_ != 0u) intr0_ |= kIntSer0;
        DriveIcuLocked();
    }

    uint32_t MmioBase() const override { return M.base; }
    uint32_t MmioSize() const override { return M.size; }

    uint16_t ReadHalf(uint32_t addr) override {
        std::lock_guard<std::mutex> lk(mtx_);
        switch (addr - M.base) {
            case kOffPort:   return portreg_;
            case kOffAsim00: return static_cast<uint16_t>(asim00_ | kAsim00FixedRead);
            case kOffAsim01: return asim01_;
            case kOffRxb0R:  return ReadReceiveBufferLocked();
            case kOffRxb0L:  return static_cast<uint16_t>(ReadReceiveBufferLocked() & kRxb0LData);
            case kOffTxs0R:  return txs0_;
            case kOffTxs0L:  return static_cast<uint16_t>(txs0_ & kTxs0LData);
            /* ASIS0REG D7 SOT0 "1: Transmission start / 0: Transmission complete", cleared
               "when the transmission is completed"; D2 PE0, D1 FE0, D0 OVE0 are receive
               error flags (VR4121 UM 23.2.9, VR4102 UM 22.2.9). */
            case kOffAsis0:  return asis0_errors_;
            case kOffIntr0:  return intr0_;
            case kOffBprm0:  return bprm0_;
            /* DSIURST is "Cleared to 0 when 1 is written" (VR4121 UM 23.2.12). */
            case kOffReset:  return 0u;
            default: return Peripheral::ReadHalf(addr);
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        bool                    drive_changed = false;
        std::optional<uint16_t> tx_data;
        std::optional<uint8_t>  to_line;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            switch (addr - M.base) {
                case kOffPort:
                    portreg_      = static_cast<uint16_t>(value & kPortWritable);
                    drive_changed = true;
                    break;
                case kOffModem: break;
                case kOffAsim00:
                    asim00_       = static_cast<uint16_t>(value & kAsim00Writable);
                    drive_changed = true;
                    break;
                case kOffAsim01: asim01_ = static_cast<uint16_t>(value & kAsim01Writable); break;
                case kOffBprm0:  bprm0_  = static_cast<uint16_t>(value & kBprmWritable);   break;
                case kOffTxs0R:
                    tx_data = BeginTransmitLocked(static_cast<uint16_t>(value & kTxs0RData));
                    break;
                case kOffTxs0L:
                    if (NineBitLocked()) {
                        HaltUnsupportedAccess("VR41xx DSIU TXS0LREG write in extended-bit mode",
                                              addr, value);
                    }
                    tx_data = BeginTransmitLocked(
                        static_cast<uint16_t>((txs0_ & kTxs08) | (value & kTxs0LData)));
                    break;
                case kOffIntr0:
                    intr0_ = static_cast<uint16_t>(intr0_ & ~(value & kIntCauses));
                    DriveIcuLocked();
                    break;
                case kOffReset:
                    /* D15:1 are RFU, "Write 0 to these bits" (VR4121 UM 23.2.12). */
                    if (value & ~kDsiuRst) {
                        HaltUnsupportedAccess("VR41xx DSIU DSIURESETREG reserved bits",
                                              addr, value);
                    }
                    if (value & kDsiuRst) {
                        ApplyDsiuResetLocked();
                        drive_changed = true;
                    }
                    break;
                default: Peripheral::WriteHalf(addr, value); break;
            }
            if (tx_data && endpoint_ != nullptr) to_line = LineDeliveryLocked(addr, *tx_data);
        }
        if (to_line) endpoint_->OnGuestTx(*to_line);
        if (tx_data && endpoint_ == nullptr) {
            emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(*tx_data & 0xFFu), tx_line_, "DSIU");
        }
        if (drive_changed) giu_->NotifyOutputDriveChanged();
    }

    void SaveState(StateWriter& w) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            w.Write("portreg", portreg_);
            w.Write("asim00", asim00_);
            w.Write("asim01", asim01_);
            w.Write("txs0", txs0_);
            w.Write("rxb0", rxb0_);
            w.Write<uint8_t>("rx_unread", rx_unread_ ? 1u : 0u);
            w.Write("asis0_errors", asis0_errors_);
            w.Write("intr0", intr0_);
            w.Write("bprm0", bprm0_);
            w.Write<uint8_t>("hibernating", hibernating_ ? 1u : 0u);
        }
        if (endpoint_ != nullptr) endpoint_->SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            uint8_t rx_unread   = 0u;
            uint8_t hibernating = 0u;
            r.Read("portreg", portreg_);
            r.Read("asim00", asim00_);
            r.Read("asim01", asim01_);
            r.Read("txs0", txs0_);
            r.Read("rxb0", rxb0_);
            r.Read("rx_unread", rx_unread);
            r.Read("asis0_errors", asis0_errors_);
            r.Read("intr0", intr0_);
            r.Read("bprm0", bprm0_);
            r.Read("hibernating", hibernating);
            rx_unread_   = rx_unread != 0u;
            hibernating_ = hibernating != 0u;
        }
        if (endpoint_ != nullptr) endpoint_->RestoreState(r);
    }

    void PostRestore() override {
        std::lock_guard<std::mutex> lk(mtx_);
        suspended_ = channel_->Suspended();
        DriveIcuLocked();
    }

private:
    std::optional<PinDrive> PortPinDrive(uint16_t general_purpose_bit, PinDrive drive) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (portreg_ & general_purpose_bit) return std::nullopt;
        return drive;
    }

    std::optional<PinDrive> DrtsDrive() {
        std::lock_guard<std::mutex> lk(mtx_);
        return DrtsDriveLocked();
    }

    std::optional<PinDrive> DrtsDriveLocked() const {
        if (portreg_ & kPortCdRts) return std::nullopt;
        if (hibernating_ || suspended_) return PinDrive::kHigh;
        return (asim00_ & kAsim00Rxe0) ? PinDrive::kLow : PinDrive::kHigh;
    }

    template <class Apply>
    void SetPowerDown(Apply apply) {
        bool changed = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            const auto before = DrtsDriveLocked();
            apply();
            changed = DrtsDriveLocked() != before;
        }
        if (changed) giu_->NotifyOutputDriveChanged();
    }

    uint16_t BeginTransmitLocked(uint16_t data) {
        txs0_ = data;
        intr0_ |= kIntSt0;
        DriveIcuLocked();
        return data;
    }

    uint16_t ReadReceiveBufferLocked() {
        rx_unread_    = false;
        asis0_errors_ = 0u;
        return rxb0_;
    }

    bool NineBitLocked() const {
        return (asim01_ & kAsim01Ebs0) != 0u && (asim00_ & kAsim00Ps) == 0u;
    }

    SerialLine::LineConfig FormatLocked(const std::array<DsiuParity, 4>& parity) const {
        SerialLine::LineConfig f;
        f.baud      = kBaud[bprm0_ & kBprm0Bpr];
        f.data_bits = (asim00_ & kAsim00Cl0) != 0u ? 8u : 7u;
        f.parity    = parity[(asim00_ & kAsim00Ps) >> kAsim00PsShift];
        f.stop      = (asim00_ & kAsim00Sl0) != 0u ? SerialLine::LineConfig::Stop::Two
                                                    : SerialLine::LineConfig::Stop::One;
        return f;
    }

    std::optional<uint8_t> LineDeliveryLocked(uint32_t addr, uint16_t data) {
        if ((bprm0_ & kBprm0Brce0) == 0u || (portreg_ & kPortCdDout) != 0u) return std::nullopt;
        if (NineBitLocked()) {
            HaltUnsupportedAccess("VR41xx DSIU line transmission in extended-bit mode", addr, data);
        }
        const uint16_t mask = (asim00_ & kAsim00Cl0) != 0u ? 0x00FFu : 0x007Fu;
        return static_cast<uint8_t>(data & mask);
    }

    /* "This register is used to reset the debug serial mode" (VR4102 UM 22.2.12).
       "If a reset is input, the transmit complete interrupt request (Dsiu_Intst0) will not
       occur even when the transmission shift register is empty" (VR4121 UM 23.3.2 Caution 1,
       VR4102 UM 22.3.2 Caution 1). */
    void ApplyDsiuResetLocked() {
        asim00_       = 0;
        asim01_       = 0;
        txs0_         = kTxs0PowerOn;
        rxb0_         = 0;
        rx_unread_    = false;
        asis0_errors_ = 0;
        intr0_        = 0;
        bprm0_        = 0;
        DriveIcuLocked();
    }

    /* Every DSIU register's RTCRST column equals its other-resets column except PORTREG's,
       whose After-reset row is "Previous value is retained" on the VR4121 (UM 23.2.1) and 0
       on the VR4102 (UM 22.2.1). */
    void ApplyResetLocked(ResetLineKind kind) {
        ApplyDsiuResetLocked();
        if (kind == ResetLineKind::Rtc || !M.portreg_retained_on_reset) portreg_ = 0;
    }

    /* The ICU's DSIUINTREG carries the DSIU's interrupt causes and raises SYSINT2REG
       DSIUINTR when unmasked (VR4121 UM 15.1, VR4102 UM 14.1). */
    void DriveIcuLocked() {
        const uint16_t causes = static_cast<uint16_t>(intr0_ & kIntCauses);
        emu_.Get<Vr41xxIcu>().SetDsiuSource(
            static_cast<uint16_t>(causes << kIcuCauseShift));
    }

    std::mutex mtx_;
    Vr41xxGiu* giu_ = nullptr;
    cerfengine::MipsInterruptChannel* channel_ = nullptr;
    UartEndpoint*                     endpoint_ = nullptr;
    std::string                       tx_line_;
    bool       hibernating_ = false;
    bool       suspended_   = false;

    uint16_t portreg_ = 0;                 /* PORTREG   RTCRST row */
    uint16_t asim00_  = 0;                 /* ASIM00REG reset row, less the D7 fixed read */
    uint16_t asim01_  = 0;                 /* ASIM01REG reset row */
    uint16_t txs0_         = kTxs0PowerOn;
    uint16_t rxb0_         = 0;
    bool     rx_unread_    = false;
    uint16_t asis0_errors_ = 0;
    uint16_t intr0_   = 0;                 /* INTR0REG  reset row */
    uint16_t bprm0_   = 0;                 /* BPRM0REG  reset row */
};

}  /* namespace cerf_vr41xx_dsiu_detail */

}
