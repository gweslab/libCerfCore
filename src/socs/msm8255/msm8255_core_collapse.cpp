#include "../core_power_collapse.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "msm8255_id.h"
#include "msm8255_saw.h"
#include "msm8255_warm_boot.h"

namespace cerfcore {

namespace {

class Msm8255CoreCollapse : public CorePowerCollapse {
public:
    using CorePowerCollapse::CorePowerCollapse;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        saw_       = &emu_.Get<Msm8255Saw>();
        warm_boot_ = &emu_.Get<Msm8255WarmBoot>();
    }

    bool ResumeIfCollapsed() override {
        if (!saw_->TakeCoreCollapse()) return false;
        warm_boot_->ResumeCollapsedCore();
        return true;
    }

private:
    Msm8255Saw*      saw_       = nullptr;
    Msm8255WarmBoot* warm_boot_ = nullptr;
};

}

REGISTER_SERVICE_AS(Msm8255CoreCollapse, CorePowerCollapse);

}
