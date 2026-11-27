#define NOMINMAX

#include "../../socs/msm8255/msm8255_mddi_client.h"

#include <cerfcore/core/board_database.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../core/board_parts.h"
#include "../../host/panel_frame_renderer.h"
#include "../../lcd/panel_scanout.h"
#include <cerfcore/boards/board_context.h>
#include "nokia_sea_ray_family_id.h"

#include <algorithm>
#include <optional>

namespace cerfcore {

namespace {

class NokiaSeaRayPanelRenderer : public PanelFrameRenderer {
public:
    using PanelFrameRenderer::PanelFrameRenderer;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
    }

    void OnReady() override {
        clamp_ = emu_.Get<BoardParts>().ViewportClamp();
    }

    void PresentedSize(uint32_t& w, uint32_t& h) override {
        const Msm8255MddiSurface s = emu_.Get<Msm8255MddiClient>().Surface();
        w = Width(s);
        h = Height(s);
    }

    bool HasFrame() override {
        return emu_.Get<Msm8255MddiClient>().Surface().visible;
    }

    void RenderInto(uint32_t* dib, uint32_t host_w, uint32_t host_h) override {
        const Msm8255MddiSurface s = emu_.Get<Msm8255MddiClient>().Surface();
        const PanelSurface src{.fb     = s.pixels,
                               .stride = s.stride_bytes,
                               .width  = Width(s),
                               .height = Height(s)};
        PanelScanout{s.format}.Blit(src, dib, host_w, host_h);
    }

private:
    uint32_t Width(const Msm8255MddiSurface& s) const {
        return clamp_ ? std::min(s.width, clamp_->width) : s.width;
    }

    uint32_t Height(const Msm8255MddiSurface& s) const {
        return clamp_ ? std::min(s.height, clamp_->height) : s.height;
    }

    std::optional<DbLcdPanelSize> clamp_;
};

}

REGISTER_SERVICE_AS(NokiaSeaRayPanelRenderer, PanelFrameRenderer);

}
