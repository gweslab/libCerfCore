#pragma once

#include "../reg_window_impl.h"

#include <cstdint>
#include <string_view>

namespace cerfcore {

namespace cerf_vr41xx_fir_detail {

using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kFirSize = 0x36u;

inline constexpr uint32_t kOffFrstr  = 0x00u;
inline constexpr uint32_t kOffDpintr = 0x02u;
inline constexpr uint32_t kOffDpcntr = 0x04u;
inline constexpr uint32_t kOffImr    = 0x14u;
inline constexpr uint32_t kOffFsr    = 0x16u;
inline constexpr uint32_t kOffIrsr1  = 0x18u;
inline constexpr uint32_t kOffCrcsr  = 0x1Cu;
inline constexpr uint32_t kOffFircr  = 0x1Eu;
inline constexpr uint32_t kOffMircr  = 0x20u;
inline constexpr uint32_t kOffDmacr  = 0x22u;
inline constexpr uint32_t kOffDmaer  = 0x24u;
inline constexpr uint32_t kOffRxir   = 0x28u;
inline constexpr uint32_t kOffIfr    = 0x2Au;
inline constexpr uint32_t kOffMrxf   = 0x30u;

inline constexpr uint16_t kFrstrFrst      = 0x0001u;
inline constexpr uint16_t kDpintrPageFlags = 0x000Fu;
inline constexpr uint16_t kDpintrFdpint5  = 0x0010u;
inline constexpr uint16_t kDpintrRfu      = 0xFFE0u;
inline constexpr uint16_t kDpcntrWritable = 0x0001u;
inline constexpr uint16_t kImrWritable    = 0x00FFu;
inline constexpr uint16_t kFsrStored      = 0x00F8u;
inline constexpr uint16_t kFsrTriggers    = 0x0007u;
inline constexpr uint16_t kFsrTxStop      = 0x0001u;
inline constexpr uint16_t kIrsr1Writable  = 0x0083u;
inline constexpr uint16_t kIrsr1IrdaEn    = 0x0080u;
inline constexpr uint16_t kCrcsrWritable  = 0x00F7u;
inline constexpr uint16_t kFircrWritable  = 0x00E7u;
inline constexpr uint16_t kFircrReadOnly  = 0x0018u;
inline constexpr uint16_t kFircrAfterReset = 0x0085u;
inline constexpr uint16_t kMircrWritable  = 0x00FFu;
inline constexpr uint16_t kMircrAfterReset = 0x0049u;
inline constexpr uint16_t kDmacrWritable  = 0x0047u;
inline constexpr uint16_t kDmaerDmaEn     = 0x0001u;
inline constexpr uint16_t kDmaerDmaBusy   = 0x0002u;
inline constexpr uint16_t kMrxfWritable   = 0x1FFFu;

constexpr RegSpec StoredSpec(uint16_t writable, uint16_t dropped, uint16_t reset) {
    return {ReadKind::kStored, WriteKind::kStored, writable, reset, static_cast<uint16_t>(~(writable | dropped))};
}

constexpr RegSpec ZeroReadSpec(WriteKind write, uint16_t writable, uint16_t fatal_on_set) {
    return {ReadKind::kZero, write, writable, 0u, fatal_on_set};
}

constexpr RegWindowModel FirModel(uint32_t base, uint16_t crcsr_after_reset, bool fdpint5_write_clears) {
    RegWindowModel m{base, kFirSize, kFirSize / 2u, false, {}};
    m.reg[kOffFrstr / 2u]  = StoredSpec(kFrstrFrst, 0u, 0u);
    m.reg[kOffDpintr / 2u] = ZeroReadSpec(WriteKind::kDrop, kDpintrPageFlags | kDpintrFdpint5,
                                          fdpint5_write_clears ? kDpintrRfu
                                                               : static_cast<uint16_t>(kDpintrRfu | kDpintrFdpint5));
    m.reg[kOffDpcntr / 2u] = StoredSpec(kDpcntrWritable, 0u, 0u);
    m.reg[kOffImr / 2u]    = StoredSpec(kImrWritable, 0u, 0u);
    m.reg[kOffFsr / 2u]    = StoredSpec(kFsrStored, kFsrTriggers, 0u);
    m.reg[kOffIrsr1 / 2u]  = StoredSpec(kIrsr1Writable, 0u, 0u);
    m.reg[kOffCrcsr / 2u]  = StoredSpec(kCrcsrWritable, 0u, crcsr_after_reset);
    m.reg[kOffFircr / 2u]  = StoredSpec(kFircrWritable, kFircrReadOnly, kFircrAfterReset);
    m.reg[kOffMircr / 2u]  = StoredSpec(kMircrWritable, 0u, kMircrAfterReset);
    m.reg[kOffDmacr / 2u]  = StoredSpec(kDmacrWritable, 0u, 0u);
    m.reg[kOffDmaer / 2u]  = ZeroReadSpec(WriteKind::kDrop, kDmaerDmaEn, static_cast<uint16_t>(~kDmaerDmaBusy));
    m.reg[kOffRxir / 2u]   = ZeroReadSpec(WriteKind::kFatal, 0u, 0u);
    m.reg[kOffIfr / 2u]    = ZeroReadSpec(WriteKind::kFatal, 0u, 0u);
    m.reg[kOffMrxf / 2u]   = StoredSpec(kMrxfWritable, 0u, 0u);
    return m;
}

template <const std::string_view& Soc, const RegWindowModel& M>
class Vr41xxFirBase : public RegWindowBase<Soc, M> {
    using Base = RegWindowBase<Soc, M>;

public:
    using Base::Base;

    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t off = addr - M.base;
        RequireReleased(addr, off, 0u);
        if ((off == kOffDpintr || off == kOffRxir || off == kOffIfr) && IrdaEnabled())
            this->HaltUnsupportedAccess("FIR ReadHalf with IRDA_EN set", addr, 0);
        return Base::ReadHalf(addr);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - M.base;
        RequireReleased(addr, off, value);
        if (off == kOffFsr && (value & kFsrTxStop) && IrdaEnabled())
            this->HaltUnsupportedAccess("FIR WriteHalf FSR TX_STOP with IRDA_EN set", addr, value);
        if ((off == kOffIrsr1 && (value & kIrsr1IrdaEn) && this->StoredReg(kOffImr / 2u) != 0u) ||
            (off == kOffImr && (value & kImrWritable) != 0u && IrdaEnabled()))
            this->HaltUnsupportedAccess("FIR WriteHalf unmasks FIR interrupt output with IRDA_EN set", addr, value);
        Base::WriteHalf(addr, value);
        if (off == kOffFrstr && (value & kFrstrFrst)) {
            this->ResetRegisterFile(false);
            this->ApplyStoredWrite(kOffFrstr, kFrstrFrst);
        }
    }

protected:
    const char* RegStateName(uint32_t i) const override {
        (void)i;
        return "fir_reg";
    }

private:
    bool IrdaEnabled() const { return (this->StoredReg(kOffIrsr1 / 2u) & kIrsr1IrdaEn) != 0u; }

    void RequireReleased(uint32_t addr, uint32_t off, uint16_t value) {
        if (off != kOffFrstr && (this->StoredReg(kOffFrstr / 2u) & kFrstrFrst) != 0u)
            this->HaltUnsupportedAccess("FIR access while FRSTR FRST holds the FIR in reset", addr, value);
    }
};

}

}
