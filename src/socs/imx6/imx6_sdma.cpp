#include <cerfcore/core/cerf_emulator.h>
#include "../freescale_sdma_impl.h"
#include "imx6_ecspi3.h"
#include "imx6_gic.h"
#include "imx6_id.h"

namespace cerfcore {

namespace {

class Imx6Sdma : public cerf_freescale_sdma_detail::FreescaleSdmaBase<0x020EC000u, SocId::Imx6dq> {
public:
    using FreescaleSdmaBase::FreescaleSdmaBase;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void OnReady() override {
        FreescaleSdmaBase::OnReady();
        emu_.Get<Imx6Ecspi3>().BindSdma(this, kEcspi3TxEvent, kEcspi3RxEvent);
    }

protected:
    void AssertIrqLine() override { emu_.Get<Imx6Gic>().AssertSpi(2); }
    void DeassertIrqLine() override { emu_.Get<Imx6Gic>().DeAssertSpi(2); }

    uint32_t ChnenblBase() const override { return 0x200u; }
    uint32_t ChnenblCount() const override { return 48u; }

    bool UnmappedRingIdles() const override { return false; }

    uint32_t BdStride(uint32_t) const override { return 12u; }

private:
    static constexpr uint32_t kEcspi3RxEvent = 7u;
    static constexpr uint32_t kEcspi3TxEvent = 8u;
};

REGISTER_SERVICE(Imx6Sdma);

}

}
