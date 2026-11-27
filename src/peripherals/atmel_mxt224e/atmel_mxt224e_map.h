#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

struct AtmelMxt224eObject {
    uint8_t  type;
    uint16_t start;
    uint8_t  size;
    uint8_t  report_ids;
};

inline constexpr uint8_t kMxtFamilyId  = 0x81u;
inline constexpr uint8_t kMxtVariantId = 0x01u;
inline constexpr uint8_t kMxtVersion   = 0x10u;
inline constexpr uint8_t kMxtBuild     = 0xAAu;
inline constexpr uint8_t kMxtMatrixX   = 16u;
inline constexpr uint8_t kMxtMatrixY   = 14u;

inline constexpr uint16_t kMxtIdBytes       = 7u;
inline constexpr uint16_t kMxtElementBytes  = 6u;
inline constexpr uint16_t kMxtObjectCount   = 18u;
inline constexpr uint16_t kMxtInfoCrcBytes  = 3u;
inline constexpr uint16_t kMxtTableEnd      = kMxtIdBytes + kMxtObjectCount * kMxtElementBytes;
inline constexpr uint16_t kMxtInfoBlockEnd  = kMxtTableEnd + kMxtInfoCrcBytes;

inline constexpr uint8_t kMxtT5  = 5u;
inline constexpr uint8_t kMxtT6  = 6u;
inline constexpr uint8_t kMxtT7  = 7u;
inline constexpr uint8_t kMxtT8  = 8u;
inline constexpr uint8_t kMxtT9  = 9u;
inline constexpr uint8_t kMxtT15 = 15u;
inline constexpr uint8_t kMxtT18 = 18u;
inline constexpr uint8_t kMxtT19 = 19u;
inline constexpr uint8_t kMxtT23 = 23u;
inline constexpr uint8_t kMxtT25 = 25u;
inline constexpr uint8_t kMxtT37 = 37u;
inline constexpr uint8_t kMxtT38 = 38u;
inline constexpr uint8_t kMxtT40 = 40u;
inline constexpr uint8_t kMxtT42 = 42u;
inline constexpr uint8_t kMxtT44 = 44u;
inline constexpr uint8_t kMxtT46 = 46u;
inline constexpr uint8_t kMxtT47 = 47u;
inline constexpr uint8_t kMxtT48 = 48u;

inline constexpr uint16_t kMxtT37Start = kMxtInfoBlockEnd;
inline constexpr uint16_t kMxtT44Start = kMxtT37Start + 130u;
inline constexpr uint16_t kMxtT5Start  = kMxtT44Start + 1u;
inline constexpr uint16_t kMxtT6Start  = kMxtT5Start + 9u;
inline constexpr uint16_t kMxtT38Start = kMxtT6Start + 6u;
inline constexpr uint16_t kMxtT7Start  = kMxtT38Start + 8u;
inline constexpr uint16_t kMxtT8Start  = kMxtT7Start + 3u;
inline constexpr uint16_t kMxtT9Start  = kMxtT8Start + 10u;
inline constexpr uint16_t kMxtT15Start = kMxtT9Start + 35u;
inline constexpr uint16_t kMxtT18Start = kMxtT15Start + 11u;
inline constexpr uint16_t kMxtT19Start = kMxtT18Start + 2u;
inline constexpr uint16_t kMxtT23Start = kMxtT19Start + 16u;
inline constexpr uint16_t kMxtT25Start = kMxtT23Start + 15u;
inline constexpr uint16_t kMxtT40Start = kMxtT25Start + 14u;
inline constexpr uint16_t kMxtT42Start = kMxtT40Start + 5u;
inline constexpr uint16_t kMxtT46Start = kMxtT42Start + 8u;
inline constexpr uint16_t kMxtT47Start = kMxtT46Start + 9u;
inline constexpr uint16_t kMxtT48Start = kMxtT47Start + 10u;
inline constexpr uint16_t kMxtMapEnd   = kMxtT48Start + 54u;

inline constexpr uint16_t kMxtT5Bytes      = kMxtT6Start - kMxtT5Start;
inline constexpr uint16_t kMxtT6Bytes      = kMxtT38Start - kMxtT6Start;
inline constexpr uint16_t kMxtMessageBytes = kMxtT5Bytes - 2u;
inline constexpr uint16_t kMxtNvmStart     = kMxtT38Start;
inline constexpr uint16_t kMxtNvmBytes     = kMxtMapEnd - kMxtNvmStart;
inline constexpr uint16_t kMxtCrcStart     = kMxtT7Start;
inline constexpr uint8_t  kMxtT9Touches    = 10u;

inline constexpr uint16_t kMxtT7IdleAcqInt = kMxtT7Start;
inline constexpr uint16_t kMxtT7ActvAcqInt = kMxtT7Start + 1u;
inline constexpr uint16_t kMxtT9Ctrl       = kMxtT9Start;
inline constexpr uint16_t kMxtT9XOrigin    = kMxtT9Start + 1u;
inline constexpr uint16_t kMxtT9NumTouch   = kMxtT9Start + 14u;
inline constexpr uint16_t kMxtT9XRange     = kMxtT9Start + 18u;
inline constexpr uint16_t kMxtT9YRange     = kMxtT9Start + 20u;
inline constexpr uint16_t kMxtT18Ctrl      = kMxtT18Start;
inline constexpr uint16_t kMxtT18Command   = kMxtT18Start + 1u;
inline constexpr uint16_t kMxtT46Mode      = kMxtT46Start + 1u;

inline constexpr uint8_t kMxtCtrlEnable   = 0x01u;
inline constexpr uint8_t kMxtT9Rpten      = 0x02u;
inline constexpr uint8_t kMxtT9Detect     = 0x80u;
inline constexpr uint8_t kMxtT18Mode      = 0x04u;
inline constexpr uint8_t kMxtT18ChgLow    = 3u;

inline constexpr uint16_t kMxtRange10BitMax = 1023u;

constexpr uint16_t MxtEffectiveRange(uint16_t range) { return range == 0u ? kMxtRange10BitMax : range; }

constexpr bool MxtRange12Bit(uint16_t effective_range) { return effective_range > kMxtRange10BitMax; }

inline constexpr std::array<AtmelMxt224eObject, kMxtObjectCount> kMxtObjects = {{
    {kMxtT37, kMxtT37Start, 130u, 0u}, {kMxtT44, kMxtT44Start, 1u, 0u},  {kMxtT5, kMxtT5Start, 9u, 0u},
    {kMxtT6, kMxtT6Start, 6u, 1u},     {kMxtT38, kMxtT38Start, 8u, 0u},  {kMxtT7, kMxtT7Start, 3u, 0u},
    {kMxtT8, kMxtT8Start, 10u, 0u},    {kMxtT9, kMxtT9Start, 35u, kMxtT9Touches},  {kMxtT15, kMxtT15Start, 11u, 1u},
    {kMxtT18, kMxtT18Start, 2u, 0u},   {kMxtT19, kMxtT19Start, 16u, 1u}, {kMxtT23, kMxtT23Start, 15u, 1u},
    {kMxtT25, kMxtT25Start, 14u, 1u},  {kMxtT40, kMxtT40Start, 5u, 0u},  {kMxtT42, kMxtT42Start, 8u, 1u},
    {kMxtT46, kMxtT46Start, 9u, 1u},   {kMxtT47, kMxtT47Start, 10u, 0u}, {kMxtT48, kMxtT48Start, 54u, 1u},
}};

inline constexpr uint8_t kMxtReportIdT6      = 1u;
inline constexpr uint8_t kMxtReportIdT9First = 2u;
inline constexpr uint8_t kMxtReportIdCount   = 18u;
inline constexpr uint8_t kMxtReportIdInvalid = 0xFFu;

static_assert(kMxtMapEnd == kMxtObjects.back().start + kMxtObjects.back().size);
static_assert(kMxtT5Bytes == 9u);

}
