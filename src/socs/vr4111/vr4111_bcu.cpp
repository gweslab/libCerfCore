#include "../vr41xx/vr41xx_bcu_impl.h"

#include "../vr41xx/vr41xx_clock_strap.h"
#include "vr4111_bus_error.h"
#include "vr4111_core_clock_model.h"

#include <cstdint>
#include "vr4111_id.h"

namespace cerfcore {

namespace {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_vr41xx_bcu_detail::Vr41xxBcuBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

/* BCU 0x0B000000-0x0B00001F, DMAA follows at 0x0B000020 (VR4111 UM Table 6-10 p170);
   register offsets from UM Table 11-1 p263. */
constexpr RegWindowModel kModel = {
    /*base=*/0x0B000000u,
    /*size=*/0x20u,
    /*num_regs=*/12u,
    true,
    {
        /* 0x00 BCUCNTREG1 (UM 11.2.1 p264): R/W D15/14/13/12/10/8/6/4/1/0,
           RFU-read-0 D11/9/7/5/3/2, RTCRST and Other-resets rows both 0. */
        { ReadKind::kStored, WriteKind::kStored, 0xF553u, 0x0000u, 0x0AACu },
        /* 0x02 BCUCNTREG2 (UM 11.2.2 p266): R/W D0 GMODE, D15:1 RFU-read-0,
           both reset rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x0001u, 0x0000u, 0xFFFEu },
        {},
        {},
        {},
        /* 0x0A BCUSPEEDREG (UM 11.2.3 p267): R/W D13:12 WPROM, D10:8 WLCD/M,
           D6:4 WISAA, D2:0 WROMA, RFU-read-0 D15/14/11/7/3, both rows 0. */
        { ReadKind::kStored, WriteKind::kStored, 0x3777u, 0x0000u, 0xC888u },
        {},
        /* 0x0E BCURFCNTREG (UM 11.2.5 p270): R/W D13:0 BRF, RFU-read-0 D15:14;
           RTCRST 0x0200; Other resets 0 on D15:14, Undefined on D13:0. */
        { ReadKind::kStored, WriteKind::kStored, 0x3FFFu, 0x0200u, 0xC000u,
          OtherReset::kReset, 0x3FFFu },
        {},
        {},
        {},
        { ReadKind::kStored, WriteKind::kFatal, 0x0000u, 0x0000u },
    },
};

using cerf_vr41xx_bcu_detail::WritableAndWrite0Partition;

static_assert(WritableAndWrite0Partition(kModel, 0x00u) &&
                  WritableAndWrite0Partition(kModel, 0x02u) &&
                  WritableAndWrite0Partition(kModel, 0x0Au) &&
                  WritableAndWrite0Partition(kModel, 0x0Eu),
              "VR4111 BCU writable and write-0 RFU bits must partition all 16");

using vr41xx_bcu::kOffSpeed;

constexpr uint16_t kWlcdmHigh = 0x0400u;

const char* SpeedEncodingRejected(uint16_t speed, uint16_t cnt1) {
    if (const char* why = vr41xx_bcu::SpeedRfuEncoding(speed)) return why;
    if ((speed & kWlcdmHigh) != 0u && (cnt1 & vr41xx_bcu::kCnt1IsamLcd) == 0u) {
        return "BCUSPEEDREG WLCD/M 1xx is RFU while BCUCNTREG1 ISAM/LCD = 0";
    }
    return nullptr;
}

/* UM 11.2.4 p269 BCUERRSTREG (0x0B00 000C): D0 BERRST R/W1C, D[15..1] Reserved R
   "Write 0 to these bits.  0 is returned after a read." */
constexpr uint32_t kErrStAddr     = 0x0B00000Cu;
constexpr uint16_t kErrStReserved = 0xFFFEu;

constexpr uint16_t kRevIdVr4111 = 0x2000u;

constexpr uint16_t kDiv2b = 0x8000u;
constexpr uint16_t kDiv3b = 0x4000u;
constexpr uint16_t kDiv4b = 0x2000u;

constexpr uint16_t DivSelectBit(uint64_t pclock_per_tclock) {
    return pclock_per_tclock == 2u ? kDiv2b : pclock_per_tclock == 3u ? kDiv3b : pclock_per_tclock == 4u ? kDiv4b : 0u;
}

constexpr bool EveryRowSelectsOneDivider() {
    for (const Vr41xxClkselRow& row : kVr4111CoreClock.rows) {
        if (DivSelectBit(Vr41xxPclockPerTclock(kVr4111CoreClock, row)) == 0u) return false;
    }
    return true;
}

static_assert(EveryRowSelectsOneDivider(), "every VR4111 CLKSEL row must give TClock = PClock / 2, 3 or 4");

class Vr4111Bcu : public Vr41xxBcuBase<SocId::Vr4111, kModel> {
public:
    using Vr41xxBcuBase::Vr41xxBcuBase;

    uint16_t ReadHalf(uint32_t addr) override {
        if (addr == kErrStAddr) return emu_.Get<Vr4111BusError>().ReadStatus();
        if (addr - kModel.base == vr41xx_bcu::kOffRevId) return kRevIdVr4111;
        if (addr - kModel.base == vr41xx_bcu::kOffClkSpeed) return ClkSpeedReg();
        return Vr41xxBcuBase::ReadHalf(addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (addr != kErrStAddr) {
            Vr41xxBcuBase::WriteHalf(addr, value);
            return;
        }
        if (value & kErrStReserved) {
            HaltUnsupportedAccess("WriteHalf sets a Reserved bit", addr, value);
        }
        emu_.Get<Vr4111BusError>().WriteStatus(value);
    }

    void SaveState(StateWriter& w) override {
        Vr41xxBcuBase::SaveState(w);
        emu_.Get<Vr4111BusError>().SaveState(w);
    }

    void RestoreState(StateReader& r) override {
        Vr41xxBcuBase::RestoreState(r);
        emu_.Get<Vr4111BusError>().RestoreState(r);
    }

    void PostRestore() override { emu_.Get<Vr4111BusError>().PostRestore(); }

private:
    uint16_t ClkSpeedReg() const {
        const Vr41xxClkselRow& row = emu_.Get<Vr41xxClockStrap>().StrapRow(kVr4111CoreClock.rows);
        const uint16_t divider = DivSelectBit(Vr41xxPclockPerTclock(kVr4111CoreClock, row));
        return static_cast<uint16_t>(((kDiv2b | kDiv3b | kDiv4b) & ~divider) | row.clksp);
    }

protected:
    const char* EncodingRejected(uint32_t off, uint16_t value) const override {
        if (off == kOffSpeed) {
            return SpeedEncodingRejected(value, StoredReg(vr41xx_bcu::kOffCnt1 / 2u));
        }
        if (off == vr41xx_bcu::kOffCnt1) {
            return SpeedEncodingRejected(StoredReg(kOffSpeed / 2u), value);
        }
        return nullptr;
    }
};

}

REGISTER_SERVICE(Vr4111Bcu);

}
