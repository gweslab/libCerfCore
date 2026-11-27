#pragma once

#if defined(_MSC_VER) && _MSC_VER < 1600
typedef unsigned int uint32_t;
#else
#include <cstdint>
#endif

namespace cerfcore {
namespace CerfVirt {

const uint32_t kFbRegWidth       = 0x00u;
const uint32_t kFbRegHeight      = 0x04u;
const uint32_t kFbRegBpp         = 0x08u;
const uint32_t kFbRegStride      = 0x0Cu;
const uint32_t kFbRegMemBasePa   = 0x14u;
const uint32_t kFbRegMemSizeTotal = 0x1Cu;

const uint32_t kFbRegPrimaryReserve = 0x20u;

const uint32_t kFbRegLogicalDpi  = 0x24u;

const uint32_t kFbRegRefreshRate = 0x28u;

const uint32_t kFbRegSystemFontHeight  = 0x2Cu;
const uint32_t kFbRegSystemFontPresent = 0x30u;

const uint32_t kFbRegCustomizationsApplied = 0x34u;

const uint32_t kFbRegClearType = 0x38u;

const uint32_t kFbClearTypeDefault = 0u;
const uint32_t kFbClearTypeOn      = 1u;
const uint32_t kFbClearTypeOff     = 2u;

const uint32_t kFbRegAllowUntrustedApps = 0x3Cu;

const uint32_t kFbRegScanoutOffset = 0x40u;

const uint32_t kFbRegLayerWidth  = 0x44u;
const uint32_t kFbRegLayerHeight = 0x48u;
const uint32_t kFbRegLayerStride = 0x4Cu;
const uint32_t kFbRegLayerBpp    = 0x50u;
const uint32_t kFbRegLayerPa     = 0x54u;

const uint32_t kFbRegOverlayFormat       = 0x58u;
const uint32_t kFbRegOverlayWidth        = 0x5Cu;
const uint32_t kFbRegOverlayHeight       = 0x60u;
const uint32_t kFbRegOverlayPa           = 0x64u;
const uint32_t kFbRegOverlayStride       = 0x68u;
const uint32_t kFbRegOverlayCbOffset     = 0x6Cu;
const uint32_t kFbRegOverlayCrOffset     = 0x70u;
const uint32_t kFbRegOverlayChromaStride = 0x74u;
const uint32_t kFbRegOverlaySrcLeft      = 0x78u;
const uint32_t kFbRegOverlaySrcTop       = 0x7Cu;
const uint32_t kFbRegOverlaySrcRight     = 0x80u;
const uint32_t kFbRegOverlaySrcBottom    = 0x84u;
const uint32_t kFbRegOverlayDstLeft      = 0x88u;
const uint32_t kFbRegOverlayDstTop       = 0x8Cu;
const uint32_t kFbRegOverlayDstRight     = 0x90u;
const uint32_t kFbRegOverlayDstBottom    = 0x94u;
const uint32_t kFbRegOverlayClockwise    = 0x98u;
const uint32_t kFbRegOverlayLayers       = 0x9Cu;
const uint32_t kFbRegBacklightPercent    = 0xA0u;
const uint32_t kFbRegOverlaySelectX      = 0xA4u;
const uint32_t kFbRegOverlaySelectY      = 0xA8u;
const uint32_t kFbRegPanelWidth          = 0xACu;
const uint32_t kFbRegPanelHeight         = 0xB0u;
const uint32_t kFbRegGpuBudgetCap        = 0xB4u;

const uint32_t kFbOverlayRgb565                = 1u;
const uint32_t kFbOverlayRgb888                = 2u;
const uint32_t kFbOverlayArgb8888              = 3u;
const uint32_t kFbOverlayYuyv                  = 4u;
const uint32_t kFbOverlayUyvy                  = 5u;
const uint32_t kFbOverlayYuv420Planar          = 6u;
const uint32_t kFbOverlayYuv420SemiPlanar      = 7u;
const uint32_t kFbOverlayYuv420SemiPlanarTiled = 8u;

const uint32_t kFbOverlayLayersBase      = 0u;
const uint32_t kFbOverlayLayersOverlay   = 1u;
const uint32_t kFbOverlayLayersBoth      = 2u;

inline uint32_t FbRowBytes(uint32_t width, uint32_t bpp) { return (width * bpp + 7u) >> 3; }

}
}
