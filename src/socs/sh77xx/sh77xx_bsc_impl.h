#pragma once

#include <cstdint>
#include <string_view>

#include "../reg_window_impl.h"
#include "sh77xx_bsc.h"

namespace cerfcore {

namespace sh77xx_bsc {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

inline constexpr uint32_t kBase = 0x1FFFFF60u;
inline constexpr uint32_t kRegs = 16u;

inline constexpr uint32_t kOffBcr1  = 0x00u;
inline constexpr uint32_t kOffBcr2  = 0x02u;
inline constexpr uint32_t kOffWcr1  = 0x04u;
inline constexpr uint32_t kOffWcr2  = 0x06u;
inline constexpr uint32_t kOffMcr   = 0x08u;
inline constexpr uint32_t kOffDcr   = 0x0Au;
inline constexpr uint32_t kOffPcr   = 0x0Cu;
inline constexpr uint32_t kOffRtcsr = 0x0Eu;
inline constexpr uint32_t kOffRtcor = 0x12u;
inline constexpr uint32_t kOffBcr3  = 0x1Eu;

inline constexpr uint16_t kBcr1EndianLittle  = 0x0800u;
inline constexpr uint16_t kBcr1A0BurstHigh   = 0x0400u;
inline constexpr uint16_t kBcr1A5Pcmcia      = 0x0002u;
inline constexpr uint16_t kBcr1A6Pcmcia      = 0x0001u;
inline constexpr uint32_t kBcr1DramTypeShift = 2u;

inline constexpr uint32_t kDramTypeSdramArea3     = 2u;
inline constexpr uint32_t kDramTypeSdramArea2And3 = 3u;

inline constexpr uint16_t kBcr2Reset     = 0x3FF0u;
inline constexpr uint16_t kWcr1Reset     = 0x3FF3u;
inline constexpr uint16_t kWcr2Reset     = 0xFFFFu;
inline constexpr uint16_t kDcrWritable   = 0xF35Eu;
inline constexpr uint16_t kBcr3Writable  = 0xBF3Fu;
inline constexpr uint16_t kBcr3WriteZero = 0x40C0u;

inline constexpr uint16_t kTpcHighBit      = 0x8000u;
inline constexpr uint16_t kBcr3Tpc3HighBit = 0x2000u;
inline constexpr uint32_t kMcrTrwlShift    = 10u;

inline constexpr uint16_t kRtcsrStored           = 0x007Bu;
inline constexpr uint16_t kRtcsrInterruptEnables = 0x0042u;
inline constexpr uint16_t kRtcorStored           = 0x00FFu;
inline constexpr uint16_t kRefreshKey            = 0xA5u;
inline constexpr uint16_t kRefreshData           = 0x00FFu;

struct Model {
    uint16_t bcr1_writable;
    uint16_t wcr1_writable;
    uint16_t mcr_writable;
    uint16_t pcr_writable;
    uint16_t pcr_write_zero;
    bool     tpc_high_codes_reserved;
};

constexpr RegSpec Stored(uint16_t writable, uint16_t reset, uint16_t write_zero) {
    return RegSpec{ReadKind::kStored, WriteKind::kStored, writable, reset, write_zero, OtherReset::kRetain};
}

constexpr RegWindowModel WindowOf(const Model& m) {
    RegWindowModel w{kBase, kRegs * 2u, kRegs, false, {}};
    w.reg[kOffBcr1 / 2u]  = Stored(m.bcr1_writable, kBcr1EndianLittle, 0u);
    w.reg[kOffBcr2 / 2u]  = Stored(kBcr2Reset, kBcr2Reset, 0u);
    w.reg[kOffWcr1 / 2u]  = Stored(m.wcr1_writable, kWcr1Reset, 0u);
    w.reg[kOffWcr2 / 2u]  = Stored(kWcr2Reset, kWcr2Reset, 0u);
    w.reg[kOffMcr / 2u]   = Stored(m.mcr_writable, 0u, 0u);
    w.reg[kOffDcr / 2u]   = Stored(kDcrWritable, 0u, 0u);
    w.reg[kOffPcr / 2u]   = Stored(m.pcr_writable, 0u, m.pcr_write_zero);
    w.reg[kOffRtcsr / 2u] = RegSpec{ReadKind::kFatal, WriteKind::kStored, kRtcsrStored, 0u,
                                    kRtcsrInterruptEnables, OtherReset::kRetain};
    w.reg[kOffRtcor / 2u] = Stored(kRtcorStored, 0u, 0u);
    w.reg[kOffBcr3 / 2u]  = Stored(kBcr3Writable, 0u, kBcr3WriteZero);
    return w;
}

template <const std::string_view& Soc, const RegWindowModel& W, const Model& M>
class Sh77xxBscBase : public RegWindowBase<Soc, W, Sh77xxBsc> {
    using Base = RegWindowBase<Soc, W, Sh77xxBsc>;

public:
    using Base::Base;

    bool AreaIsSdram(uint32_t area) const override {
        const uint32_t dram_type = DramType(this->StoredReg(kOffBcr1 / 2u));
        if (area == 3u) return dram_type == kDramTypeSdramArea3 || dram_type == kDramTypeSdramArea2And3;
        return area == 2u && dram_type == kDramTypeSdramArea2And3;
    }

    bool AreaIsPcmcia(uint32_t area) const override {
        return (this->StoredReg(kOffBcr1 / 2u) & (area == 5u ? kBcr1A5Pcmcia : kBcr1A6Pcmcia)) != 0u;
    }

    uint32_t AreaBusBytes(uint32_t area) const override {
        return 1u << (((this->StoredReg(kOffBcr2 / 2u) >> (2u * area)) & 3u) - 1u);
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        const uint32_t off = addr - W.base;
        if (off == kOffRtcsr || off == kOffRtcor) {
            if ((value >> 8) != kRefreshKey) {
                this->HaltUnsupportedAccess("WriteHalf of a refresh register without the H'A5 key", addr, value);
            }
            value = static_cast<uint16_t>(value & kRefreshData);
        }
        if (const char* why = WriteRejected(off, static_cast<uint16_t>(value & W.reg[off / 2u].wmask))) {
            this->HaltUnsupportedAccess(why, addr, value);
        }
        Base::WriteHalf(addr, value);
    }

private:
    static uint32_t DramType(uint16_t bcr1) { return (bcr1 >> kBcr1DramTypeShift) & 7u; }

    const char* WriteRejected(uint32_t off, uint16_t v) const {
        if (off == kOffBcr1) {
            const uint32_t dram_type = DramType(v);
            if (dram_type == 1u || dram_type == 6u || dram_type == 7u) return "BCR1 DRAMTP selects a reserved memory type";
            if ((v & kBcr1A0BurstHigh) != 0u) {
                return "BCR1 A0BST selects a burst length that the unmodeled area 0 bus width limits";
            }
        } else if (off == kOffBcr2) {
            for (uint32_t area = 2u; area <= 6u; ++area) {
                if (((v >> (2u * area)) & 3u) == 0u) return "BCR2 AnSZ selects the reserved bus size 00";
            }
        } else if (off == kOffMcr) {
            if (((v >> kMcrTrwlShift) & 3u) == 3u) return "MCR TRWL selects the reserved value 11";
            if (M.tpc_high_codes_reserved && (v & kTpcHighBit) != 0u) return "MCR TPC selects a reserved precharge time";
        } else if (off == kOffDcr) {
            if (M.tpc_high_codes_reserved && (v & kTpcHighBit) != 0u) return "DCR TPC selects a reserved precharge time";
        } else if (off == kOffBcr3) {
            if (M.tpc_high_codes_reserved && (v & kBcr3Tpc3HighBit) != 0u) {
                return "BCR3 TPC3 selects a reserved precharge time";
            }
        }
        return nullptr;
    }
};

}

}
