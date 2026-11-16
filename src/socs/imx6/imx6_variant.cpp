#include "imx6_variant.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6DqVariant final : public Imx6Variant {
public:
    using Imx6Variant::Imx6Variant;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Imx6dq; }
    Imx6Family Family() const override { return Imx6Family::DualQuad; }
    bool PeriphClk2UsesPll2Bypass() const override { return false; }
    bool Gpu2dUsesSdlTree() const override { return false; }
    bool HasGpu3dShaderPodf() const override { return true; }
    bool HasMlbSysPodf() const override { return false; }
    uint32_t CcmCbcmrReset() const override { return 0x00020324u; }
    uint32_t CcmCscdr2Reset() const override { return 0x0002A150u; }
    uint32_t CbcmrConsumedMask() const override { return 0x1F8F0030u; }
    uint32_t GpuSelectorShift(bool gpu3d) const override { return gpu3d ? 4u : 16u; }
    uint32_t GpuDividerShift(bool gpu3d) const override { return gpu3d ? 26u : 23u; }
    uint32_t Pl310CacheId() const override { return 0x410000C7u; }
    uint32_t Pl310CacheType() const override { return 0x9E340340u; }
    uint32_t Pl310AuxiliaryReset() const override { return 0x02070000u; }
    uint32_t AnatopDigprog() const override { return 0x00630000u; }
    uint32_t IomuxcGpr11Reset() const override { return 0x00003800u; }
    uint32_t IomuxcGpr13Reset() const override { return 0x059124C4u; }
    uint32_t OcramLast() const override { return 0x0093FFFFu; }
    bool IomuxcIsRegister(uint32_t offset) const override {
        return (offset >= 0x04Cu && offset <= 0x7E8u) || (offset >= 0x7F0u && offset <= 0x94Cu);
    }
};

class Imx6SdlVariant final : public Imx6Variant {
public:
    using Imx6Variant::Imx6Variant;
    bool ShouldRegister() override { return emu_.Get<BoardContext>().GetSocId() == SocId::Imx6sdl; }
    Imx6Family Family() const override { return Imx6Family::SoloDualLite; }
    bool PeriphClk2UsesPll2Bypass() const override { return true; }
    bool Gpu2dUsesSdlTree() const override { return true; }
    bool HasGpu3dShaderPodf() const override { return false; }
    bool HasMlbSysPodf() const override { return true; }
    uint32_t CcmCbcmrReset() const override { return 0x00820324u; }
    uint32_t CcmCscdr2Reset() const override { return 0x00029B48u; }
    uint32_t CbcmrConsumedMask() const override { return 0xFC0C0330u; }
    uint32_t GpuSelectorShift(bool gpu3d) const override { return gpu3d ? 4u : 8u; }
    uint32_t GpuDividerShift(bool gpu3d) const override { return gpu3d ? 26u : 29u; }
    uint32_t Pl310CacheId() const override { return 0x410000C8u; }
    uint32_t Pl310CacheType() const override { return 0x9E240240u; }
    uint32_t Pl310AuxiliaryReset() const override { return 0x02050000u; }
    uint32_t AnatopDigprog() const override { return 0x00610000u; }
    uint32_t IomuxcGpr11Reset() const override { return 0u; }
    uint32_t IomuxcGpr13Reset() const override { return 8u; }
    uint32_t OcramLast() const override { return 0x0091FFFFu; }
    bool IomuxcIsRegister(uint32_t offset) const override {
        return (offset >= 0x04Cu && offset <= 0x7CCu) || (offset >= 0x7D4u && offset <= 0x8D4u) ||
               (offset >= 0x8DCu && offset <= 0x938u);
    }
};

}

REGISTER_SERVICE_AS(Imx6DqVariant, Imx6Variant);
REGISTER_SERVICE_AS(Imx6SdlVariant, Imx6Variant);

}
