#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

enum class Imx6Family { SoloDualLite, DualQuad };

class Imx6Variant : public Service {
public:
    using Service::Service;

    virtual Imx6Family Family() const = 0;
    virtual bool PeriphClk2UsesPll2Bypass() const = 0;
    virtual bool Gpu2dUsesSdlTree() const = 0;
    virtual bool HasGpu3dShaderPodf() const = 0;
    virtual bool HasMlbSysPodf() const = 0;
    virtual uint32_t CcmCbcmrReset() const = 0;
    virtual uint32_t CcmCscdr2Reset() const = 0;
    virtual uint32_t CbcmrConsumedMask() const = 0;
    virtual uint32_t GpuSelectorShift(bool gpu3d) const = 0;
    virtual uint32_t GpuDividerShift(bool gpu3d) const = 0;
    virtual uint32_t Pl310CacheId() const = 0;
    virtual uint32_t Pl310CacheType() const = 0;
    virtual uint32_t Pl310AuxiliaryReset() const = 0;
    virtual uint32_t AnatopDigprog() const = 0;
    virtual uint32_t IomuxcGpr11Reset() const = 0;
    virtual uint32_t IomuxcGpr13Reset() const = 0;
    virtual bool IomuxcIsRegister(uint32_t offset) const = 0;
    virtual uint32_t OcramLast() const = 0;
};

}
