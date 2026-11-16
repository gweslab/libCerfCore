#include "../freescale_module_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/callback_list.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx6_ccm.h"
#include "imx6_id.h"

#include <cstdint>
#include <functional>
#include <utility>

namespace cerfcore {

namespace {

struct ClockRef { uint32_t ccgr; uint32_t cg; };

struct ModuleClocks {
    FreescaleModule module;
    ClockRef        clocks[2];
    uint32_t        clock_count;
};

constexpr ModuleClocks kModules[] = {
    {FreescaleModule::kUart1,    {{5, 12}, {5, 13}}, 2},
    {FreescaleModule::kUart2,    {{5, 12}, {5, 13}}, 2},
    {FreescaleModule::kUart1Ipg, {{5, 12}},          1},
    {FreescaleModule::kUart2Ipg, {{5, 12}},          1},
    {FreescaleModule::kSdma,     {{5, 3}},           1},
    {FreescaleModule::kPwm1,     {{4, 8}},           1},
    {FreescaleModule::kPwm2,     {{4, 9}},           1},
    {FreescaleModule::kPwm3,     {{4, 10}},          1},
    {FreescaleModule::kPwm4,     {{4, 11}},          1},
};

struct SystemClock { const char* name; ClockRef clock; };

constexpr SystemClock kSystemClocks[] = {
    {"mmdc_ch0_axi", {3, 10}}, {"mmdc_p0_ipg", {3, 12}}, {"rom", {5, 0}},
};

class Imx6ModuleClocks : public FreescaleModuleClocks {
public:
    using FreescaleModuleClocks::FreescaleModuleClocks;

    bool ShouldRegister() override {
        return SocId::IsImx6(emu_.Get<BoardContext>().GetSocId());
    }

    void OnReady() override {
        ccm_ = &emu_.Get<Imx6Ccm>();
        ccm_->RegisterGateListener([this] { OnGatesChanged(); });
        OnGatesChanged();
    }

    bool ModuleRunsIn(FreescaleModule module, FreescaleLowPowerMode mode) const override {
        const ModuleClocks& m = Row(module);
        for (uint32_t c = 0u; c < m.clock_count; ++c) {
            if (!ccm_->ClockRunsIn(m.clocks[c].ccgr, m.clocks[c].cg, mode)) return false;
        }
        return true;
    }

    void RegisterGateListener(std::function<void()> fn) override {
        listeners_.Add(std::move(fn));
    }

private:
    const ModuleClocks& Row(FreescaleModule module) const {
        for (const ModuleClocks& m : kModules) {
            if (m.module == module) return m;
        }
        emu_.Get<Fatal>().Die("Imx6ModuleClocks: module %u has no i.MX6 clock mapping",
                              static_cast<unsigned>(module));
    }

    void OnGatesChanged() {
        for (const SystemClock& s : kSystemClocks) {
            if (!ccm_->ClockRunsIn(s.clock.ccgr, s.clock.cg, FreescaleLowPowerMode::kRun)) {
                emu_.Get<Fatal>().Die("Imx6ModuleClocks: CCGR%u CG%u (%s) is off in run mode; "
                                      "stopping that clock is not modeled", s.clock.ccgr,
                                      s.clock.cg, s.name);
            }
        }
        listeners_.Call();
    }

    Imx6Ccm*                           ccm_ = nullptr;
    CallbackList<>                     listeners_;
};

}

REGISTER_SERVICE_AS(Imx6ModuleClocks, FreescaleModuleClocks);

}
