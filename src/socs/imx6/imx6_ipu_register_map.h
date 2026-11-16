#pragma once

#include <cstdint>

namespace cerfcore {

namespace imx6_ipu {

constexpr uint32_t kBase = 0x02400000u;
constexpr uint32_t kSize = 0x00300000u;
constexpr uint32_t kCmBase = 0x02600000u;
constexpr uint32_t kCmSize = 0x00100000u;
constexpr uint32_t kOffConf = 0x00000000u;
constexpr uint32_t kOffFsDispFlow1 = 0x000000B4u;
constexpr uint32_t kOffFsDispFlow2 = 0x000000B8u;
constexpr uint32_t kOffDispGen = 0x000000C4u;
constexpr uint32_t kOffMemRst = 0x000000DCu;
constexpr uint32_t kOffSrmPri1 = 0x000000A0u;
constexpr uint32_t kOffSrmPri2 = 0x000000A4u;
constexpr uint32_t kOffFsProcFlow1 = 0x000000A8u;
constexpr uint32_t kOffFsProcFlow2 = 0x000000ACu;
constexpr uint32_t kOffFsProcFlow3 = 0x000000B0u;
constexpr uint32_t kOffSkip = 0x000000BCu;
constexpr uint32_t kOffDispAltConf = 0x000000C0u;
constexpr uint32_t kOffDispAlt1 = 0x000000C8u;
constexpr uint32_t kOffDispAlt2 = 0x000000CCu;
constexpr uint32_t kOffDispAlt3 = 0x000000D0u;
constexpr uint32_t kOffDispAlt4 = 0x000000D4u;
constexpr uint32_t kOffSnoop = 0x000000D8u;
constexpr uint32_t kOffPm = 0x000000E0u;
constexpr uint32_t kRstSrmPri1 = 0x00000100u;
constexpr uint32_t kRstSrmPri2 = 0x06050803u;
constexpr uint32_t kRstDispGen = 0x00400000u;
constexpr uint32_t kRstDispAlt1 = 0x00400000u;
constexpr uint32_t kRstDispAlt2 = 0x00000000u;
constexpr uint32_t kRstDispAlt4 = 0x00000000u;
constexpr uint32_t kRstPm = 0x08100810u;
constexpr uint32_t kRstDi0General = 0x00200000u;
constexpr uint32_t kOffGpr = 0x000000E4u;
constexpr uint32_t kRstMemStart = 1u << 31;
constexpr uint32_t kOffIntCtrl0 = 0x0000003Cu;
constexpr uint32_t kOffIntStat0 = 0x00000200u;
constexpr uint32_t kOffChaCurBuf0 = 0x0000023Cu;
constexpr uint32_t kOffTripleCurBuf1 = 0x0000025Cu;
constexpr uint32_t kOffChDbModeSel0 = 0x00000150u;
constexpr uint32_t kOffChDbModeSel1 = 0x00000154u;
constexpr uint32_t kOffAltChDbModeSel0 = 0x00000168u;
constexpr uint32_t kOffAltChDbModeSel1 = 0x0000016Cu;
constexpr uint32_t kOffChTrbModeSel0 = 0x00000178u;
constexpr uint32_t kOffChTrbModeSel1 = 0x0000017Cu;
constexpr uint32_t kOffChaBuf0Rdy0 = 0x00000268u;
constexpr uint32_t kOffChaBuf1Rdy0 = 0x00000270u;
constexpr uint32_t kOffIdmacChEn1 = 0x00008004u;
constexpr uint32_t kOffIdmacChEn2 = 0x00008008u;
constexpr uint32_t kOffIdmacBusy1 = 0x00008100u;
constexpr uint32_t kOffIdmacBusy2 = 0x00008104u;
constexpr uint32_t kIdmacChBusy1StubValue = 0u;
constexpr uint32_t kIdmacChBusy2StubValue = 0u;
constexpr uint32_t kIcIdmac1GuestClearMask = (1u << 0) | (1u << 1) | (1u << 3) | (1u << 6) | (1u << 7) |
                                              (1u << 24) | (1u << 25);
constexpr uint32_t kIcIdmac4GuestValue = 0x5555u;
constexpr uint32_t kOffIdmacConf = 0x00008000u;
constexpr uint32_t kOffIdmacSepAlpha = 0x0000800Cu;
constexpr uint32_t kOffIdmacAltSepAlpha = 0x00008010u;
constexpr uint32_t kOffIdmacChaPri1 = 0x00008014u;
constexpr uint32_t kOffIdmacChaPri2 = 0x00008018u;
constexpr uint32_t kOffIdmacWmEn1 = 0x0000801Cu;
constexpr uint32_t kOffIdmacWmEn2 = 0x00008020u;
constexpr uint32_t kOffIdmacLockEn1 = 0x00008024u;
constexpr uint32_t kOffIdmacLockEn2 = 0x00008028u;
constexpr uint32_t kOffIdmacSubAddr0 = 0x0000802Cu;
constexpr uint32_t kOffIdmacSubAddr1 = 0x00008030u;
constexpr uint32_t kOffIdmacSubAddr2 = 0x00008034u;
constexpr uint32_t kOffIdmacSubAddr3 = 0x00008038u;
constexpr uint32_t kOffIdmacSubAddr4 = 0x0000803Cu;
constexpr uint32_t kOffIdmacBandEn1 = 0x00008040u;
constexpr uint32_t kOffIdmacBandEn2 = 0x00008044u;
constexpr uint32_t kOffIdmacScCord = 0x00008048u;
constexpr uint32_t kOffIdmacScCord1 = 0x0000804Cu;
constexpr uint32_t kRstIdmacConf = 0x0000002Fu;
constexpr uint32_t kOffIcBase = 0x00020000u;
constexpr uint32_t kOffIcEnd = 0x00020200u;
constexpr uint32_t kOffDcBase = 0x00058000u;
constexpr uint32_t kOffDcEnd = 0x00058200u;
constexpr uint32_t kOffDmfcBase = 0x00060000u;
constexpr uint32_t kOffDmfcEnd = 0x00060040u;
constexpr uint32_t kOffDi0BsClkgen0 = 0x00040004u;
constexpr uint32_t kOffDi0BsClkgen1 = 0x00040008u;
constexpr uint32_t kOffDi0SwGen0_1 = 0x0004000Cu;
constexpr uint32_t kOffDi0SwGen0_2 = 0x00040010u;
constexpr uint32_t kOffDi0SwGen0_3 = 0x00040014u;
constexpr uint32_t kOffDi0SwGen0_4 = 0x00040018u;
constexpr uint32_t kOffDi0SwGen0_5 = 0x0004001Cu;
constexpr uint32_t kOffDi0SwGen1_1 = 0x00040030u;
constexpr uint32_t kOffDi0SwGen1_2 = 0x00040034u;
constexpr uint32_t kOffDi0SwGen1_3 = 0x00040038u;
constexpr uint32_t kOffDi0SwGen1_4 = 0x0004003Cu;
constexpr uint32_t kOffDi0SwGen1_5 = 0x00040040u;
constexpr uint32_t kOffDi0SyncAsGen = 0x00040054u;
constexpr uint32_t kOffDi0DwGen3 = 0x00040064u;
constexpr uint32_t kOffDi0DwSet0Gen3 = 0x00040094u;
constexpr uint32_t kOffDi0DwSet3Gen3 = 0x00040124u;
constexpr uint32_t kOffDi0StpRep1 = 0x00040148u;
constexpr uint32_t kOffDi0StpRep2 = 0x0004014Cu;
constexpr uint32_t kOffDi0StpRep3 = 0x00040150u;
constexpr uint32_t kOffDi0Pol = 0x00040164u;
constexpr uint32_t kOffDi0ScrConf = 0x00040170u;
constexpr uint32_t kOffDcRl0Ch1 = 0x00058024u;
constexpr uint32_t kOffDcRl1Ch1 = 0x00058028u;
constexpr uint32_t kOffDcRl2Ch1 = 0x0005802Cu;
constexpr uint32_t kOffDcRl3Ch1 = 0x00058030u;
constexpr uint32_t kOffDcRl4Ch1 = 0x00058034u;
constexpr uint32_t kOffDcWrChConf6 = 0x00058078u;
constexpr uint32_t kOffDcWrChConf5 = 0x0005805Cu;
constexpr uint32_t kOffDcRl0Ch5 = 0x00058064u;
constexpr uint32_t kOffDcRl1Ch5 = 0x00058068u;
constexpr uint32_t kOffDcRl2Ch5 = 0x0005806Cu;
constexpr uint32_t kOffDcRl3Ch5 = 0x00058070u;
constexpr uint32_t kOffDcRl4Ch5 = 0x00058074u;
constexpr uint32_t kOffDcMapConf0 = 0x00058108u;
constexpr uint32_t kOffDcMapConf15 = 0x00058144u;
constexpr uint32_t kOffDcMapConf16 = 0x00058148u;
constexpr uint32_t kOffDmfcWrChan = 0x00060004u;
constexpr uint32_t kOffDmfcRdChan = 0x00060000u;
constexpr uint32_t kOffDmfcWrChanDef = 0x00060008u;
constexpr uint32_t kOffDmfcDpChan = 0x0006000Cu;
constexpr uint32_t kOffDmfcDpChanDef = 0x00060010u;
constexpr uint32_t kOffDmfcGeneral1 = 0x00060014u;
constexpr uint32_t kOffDmfcGeneral2 = 0x00060018u;
constexpr uint32_t kRstDmfcRdChan = 0x00000200u;
constexpr uint32_t kRstDmfcWrChan = 0x00000000u;
constexpr uint32_t kOffDcWrChConf1 = 0x0005801Cu;
constexpr uint32_t kOffDcWrChAddr1 = 0x00058020u;
constexpr uint32_t kOffDcWrChAddr5 = 0x00058060u;
constexpr uint32_t kOffDcGen = 0x000580D4u;
constexpr uint32_t kOffDcDispConf1_0 = 0x000580D8u;
constexpr uint32_t kOffDcDispConf2_0 = 0x000580E8u;
constexpr uint32_t kOffDcUgde0 = 0x00058174u;
constexpr uint32_t kOffDcUgde1 = 0x00058184u;
constexpr uint32_t kOffDcUgde2 = 0x00058194u;
constexpr uint32_t kOffDcUgde3 = 0x000581A4u;
constexpr uint32_t kOffDcStat = 0x000581C8u;
constexpr uint32_t kDcStatResetValue = 0x000000AAu;
constexpr uint32_t kOffDpComConfSync = 0x00018000u;
constexpr uint32_t kOffDpGraphWindCtrlSync = 0x00018004u;
constexpr uint32_t kOffDpFgPosSync = 0x00018008u;
constexpr uint32_t kOffDi0Base = 0x00040000u;
constexpr uint32_t kOffDi0General = 0x00040000u;
constexpr uint32_t kOffDi1Base = 0x00048000u;
constexpr uint32_t kOffDiSize = 0x00000200u;
constexpr uint32_t kOffVdiBase = 0x00068000u;
constexpr uint32_t kOffVdiC = 0x00068004u;
constexpr uint32_t kOffVdiEnd = 0x00069000u;
constexpr uint32_t kVdiGuestFieldMask = 0x0E387FF0u;
enum class DisplayChannelRole : uint8_t { MainPlane, AuxiliaryPlane, AlphaPlane, Command, Mask };
struct DisplayChannel {
    uint32_t number;
    DisplayChannelRole role;
};
constexpr DisplayChannel kDisplayChannels[] = {
    {23u, DisplayChannelRole::MainPlane},
    {24u, DisplayChannelRole::MainPlane},
    {27u, DisplayChannelRole::AuxiliaryPlane},
    {28u, DisplayChannelRole::MainPlane},
    {29u, DisplayChannelRole::AuxiliaryPlane},
    {31u, DisplayChannelRole::AlphaPlane},
    {33u, DisplayChannelRole::AlphaPlane},
    {41u, DisplayChannelRole::Command},
    {42u, DisplayChannelRole::Mask},
    {43u, DisplayChannelRole::Command},
    {44u, DisplayChannelRole::Mask},
    {51u, DisplayChannelRole::AlphaPlane},
    {52u, DisplayChannelRole::AlphaPlane},
};
constexpr int kIpuSyncSpi = 6;
constexpr int kIpuErrSpi = 5;
constexpr uint32_t kIpuIrqVsyncPre0 = 448u + 14u;
constexpr uint32_t kIpuIrqVsyncPre1 = 448u + 15u;

inline bool IsIpuIntCtrl(uint32_t offset) {
    return offset >= kOffIntCtrl0 && offset < kOffIntCtrl0 + 15u * 4u;
}
inline bool IsIpuIntStat(uint32_t offset) {
    return offset >= kOffIntStat0 && offset < kOffIntStat0 + 15u * 4u;
}
inline bool IsIpuCurBuf(uint32_t offset) {
    return offset == kOffChaCurBuf0 || offset == kOffChaCurBuf0 + 4u;
}
inline bool IsIpuBufReady(uint32_t offset) {
    return offset == kOffChaBuf0Rdy0 || offset == kOffChaBuf0Rdy0 + 4u || offset == kOffChaBuf1Rdy0 ||
           offset == kOffChaBuf1Rdy0 + 4u;
}
inline bool IsDcOff(uint32_t offset) {
    return offset >= kOffDcBase && offset < kOffDcEnd;
}
inline bool IsDmfcOff(uint32_t offset) {
    return offset >= kOffDmfcBase && offset < kOffDmfcEnd;
}
inline bool IsIcOff(uint32_t offset) {
    return offset >= kOffIcBase && offset < kOffIcEnd;
}
inline bool IsDiOff(uint32_t offset) {
    return (offset >= kOffDi0Base && offset < kOffDi0Base + kOffDiSize) ||
           (offset >= kOffDi1Base && offset < kOffDi1Base + kOffDiSize);
}
inline bool IsVdiOff(uint32_t offset) {
    return offset >= kOffVdiBase && offset < kOffVdiEnd;
}
inline bool IsDcMapConf(uint32_t offset) {
    return offset >= kOffDcMapConf0 && offset <= 0x00058170u;
}
constexpr uint32_t kOffChaBuf2Rdy0 = 0x00000288u;
constexpr uint32_t kOffChaBuf2Rdy1 = 0x0000028Cu;

inline bool IsIpuBuf2Ready(uint32_t offset) {
    return offset == kOffChaBuf2Rdy0 || offset == kOffChaBuf2Rdy1;
}

inline bool IsMeasuredBlockRead(uint32_t offset) {
    if (IsDcMapConf(offset)) return true;
    switch (offset) {
    case kOffIcBase: case kOffIcBase + 0x18u: case kOffDcStat:
    case kOffDi0BsClkgen0: case kOffDi0BsClkgen1:
    case kOffDi0SyncAsGen: case kOffDi0StpRep1: case kOffDi0StpRep2: case kOffDi0StpRep3:
    case kOffDi0Pol: case kOffDi0ScrConf:
    case kOffVdiC:
    case kOffDcRl0Ch1: case kOffDcRl1Ch1: case kOffDcRl2Ch1: case kOffDcRl3Ch1: case kOffDcRl4Ch1:
    case kOffDcWrChConf1: case kOffDcWrChConf5: case kOffDcWrChConf6: case kOffDcRl0Ch5: case kOffDcRl1Ch5: case kOffDcRl2Ch5: case kOffDcRl3Ch5:
    case kOffDcRl4Ch5: case kOffDcMapConf0: case kOffDcMapConf15: case kOffDcMapConf16:
    case kOffDmfcRdChan: case kOffDmfcWrChan: case kOffDmfcWrChanDef: case kOffDmfcDpChan: case kOffDmfcDpChanDef:
    case kOffDmfcGeneral1: return true;
    default: return false;
    }
}

inline bool IsMeasuredBlockWrite(uint32_t offset) {
    if (IsDcMapConf(offset)) return true;
    switch (offset) {
    case kOffIcBase: case kOffIcBase + 0x18u:
    case kOffIcBase + 0x24u:
    case kOffDi0General:
    case kOffDi0SwGen0_1: case kOffDi0SwGen0_2: case kOffDi0SwGen0_3:
    case kOffDi0BsClkgen0: case kOffDi0BsClkgen1:
    case kOffDi0ScrConf: case kOffDi0SyncAsGen: case kOffDi0Pol:
    case kOffVdiC:
    case kOffDi0StpRep1: case kOffDi0StpRep2: case kOffDi0StpRep3:
    case kOffDi0SwGen0_4: case kOffDi0SwGen0_5: case kOffDi0SwGen1_1: case kOffDi0SwGen1_2:
    case kOffDi0SwGen1_3: case kOffDi0SwGen1_4: case kOffDi0SwGen1_5: case kOffDi0DwGen3:
    case kOffDi0DwSet0Gen3: case kOffDi0DwSet3Gen3:
    case kOffDcRl0Ch1: case kOffDcRl1Ch1: case kOffDcRl2Ch1: case kOffDcRl3Ch1: case kOffDcRl4Ch1:
    case kOffDcWrChConf1: case kOffDcWrChConf5: case kOffDcWrChConf6: case kOffDcWrChAddr1: case kOffDcWrChAddr5: case kOffDcGen:
    case kOffDcRl0Ch5: case kOffDcRl1Ch5: case kOffDcRl2Ch5: case kOffDcRl3Ch5: case kOffDcRl4Ch5:
    case kOffDcMapConf0: case kOffDcMapConf15: case kOffDcMapConf16:
    case kOffDcDispConf1_0: case kOffDcDispConf2_0: case kOffDcUgde0: case kOffDcUgde1:
    case kOffDcUgde2: case kOffDcUgde3: case kOffDmfcRdChan: case kOffDmfcWrChan:
    case kOffDmfcDpChan: case kOffDmfcWrChanDef: case kOffDmfcDpChanDef:
    case kOffDmfcGeneral1: case kOffDmfcGeneral2: return true;
    default: return false;
    }
}

inline bool IsCoreRegister(uint32_t offset) {
    if (offset == kOffConf || offset == kOffFsDispFlow1 || offset == kOffFsDispFlow2 || offset == kOffDispGen ||
        offset == kOffMemRst || offset == kOffGpr || offset == kOffChDbModeSel0 ||
        offset == kOffChDbModeSel1 || offset == kOffTripleCurBuf1 || IsIpuIntCtrl(offset) ||
        IsIpuIntStat(offset) || IsIpuCurBuf(offset) ||
        IsIpuBufReady(offset) || IsIpuBuf2Ready(offset))
        return true;
    switch (offset) {
    case kOffSrmPri1: case kOffSrmPri2: case kOffFsProcFlow1: case kOffFsProcFlow2:
    case kOffFsProcFlow3: case kOffSkip: case kOffDispAltConf: case kOffDispAlt1:
    case kOffDispAlt2: case kOffDispAlt3: case kOffDispAlt4: case kOffSnoop: case kOffPm:
    case kOffIdmacConf: case kOffIdmacSepAlpha: case kOffIdmacAltSepAlpha: case kOffIdmacChaPri1:
    case kOffIdmacChaPri2: case kOffIdmacWmEn1: case kOffIdmacWmEn2: case kOffIdmacLockEn1:
    case kOffIdmacLockEn2: case kOffIdmacSubAddr0: case kOffIdmacSubAddr1: case kOffIdmacSubAddr2:
    case kOffIdmacSubAddr3: case kOffIdmacSubAddr4: case kOffIdmacBandEn1: case kOffIdmacBandEn2:
    case kOffIdmacScCord: case kOffIdmacScCord1:
    case kOffAltChDbModeSel0: case kOffAltChDbModeSel1: case kOffChTrbModeSel0: case kOffChTrbModeSel1:
    case kOffIdmacChEn1: case kOffIdmacChEn2: case kOffIdmacBusy1: case kOffIdmacBusy2:
        return true;
    default:
        return false;
    }
}

inline bool IsModelledRead(uint32_t offset) { return IsCoreRegister(offset) || IsMeasuredBlockRead(offset); }

inline bool IsModelledWrite(uint32_t offset) { return IsCoreRegister(offset) || IsMeasuredBlockWrite(offset); }

inline uint32_t DisplayChannelMask() {
    uint32_t mask = 0;
    for (const DisplayChannel& channel : kDisplayChannels)
        if (channel.number < 32u) mask |= 1u << channel.number;
    return mask;
}
inline uint32_t DisplayChannelMaskHigh() {
    uint32_t mask = 0;
    for (const DisplayChannel& channel : kDisplayChannels)
        if (channel.number >= 32u) mask |= 1u << (channel.number - 32u);
    return mask;
}

}

}
