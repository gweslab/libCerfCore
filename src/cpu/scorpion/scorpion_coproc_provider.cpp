#include "../armv7a_coproc_provider_base.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../execution/engine_bridge.h"
#include "../../socs/irq_controller.h"
#include "../../socs/msm8255/msm8255_id.h"
#include "../arm/arm_performance_monitors.h"

#include <cerfengine/arm/arm_cpu.h>
#include <cerfengine/arm/cpu_state.h>

#include <array>
#include <cstdint>

namespace cerfcore {

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

constexpr uint32_t kAvsModuleEnables    = 0x61u;
constexpr uint32_t kFpexcEnable         = 1u << 30;
constexpr uint32_t kRegionRegisters     = 4u;
constexpr uint32_t kL2RegionSelection   = 3u;
constexpr uint32_t kVenumRegionRegister = 11u;

class ScorpionCoprocProvider : public Armv7aCoprocProviderBase {
public:
    using Armv7aCoprocProviderBase::Armv7aCoprocProviderBase;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetSocId() == SocId::Msm8255;
    }

    void OnReady() override {
        Armv7aCoprocProviderBase::OnReady();
        pmu_ = &emu_.Get<ArmPerformanceMonitors>();
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
    }

    void SaveState(StateWriter& w) const override {
        w.Write("scorpion_region_selection", region_selection_);
        w.Write<uint32_t>("scorpion_venum_region_selection", venum_region_selection_);
        w.Write("scorpion_counter_control", counter_control_);
        w.Write<uint32_t>("scorpion_pmu_c9_c15_2", pmu_c9_c15_2_);
        w.Write<uint32_t>("scorpion_avs_delay_synthesizer", avs_delay_synthesizer_);
        w.Write<uint32_t>("scorpion_avs_control", avs_control_);
    }

    void RestoreState(StateReader& r) override {
        r.Read("scorpion_region_selection", region_selection_);
        r.Read("scorpion_venum_region_selection", venum_region_selection_);
        r.Read("scorpion_counter_control", counter_control_);
        r.Read("scorpion_pmu_c9_c15_2", pmu_c9_c15_2_);
        r.Read("scorpion_avs_delay_synthesizer", avs_delay_synthesizer_);
        r.Read("scorpion_avs_control", avs_control_);
    }

protected:
    /* ARM DDI 0406C.c Figure B3-35 (p. B3-1477); Cache and TCM lockdown
       registers, VMSA (p. B4-1753). */
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num == 15u && a.crn == 9u) {
            if (!a.read && a.opc1 == 0u && a.crm == 0u && a.opc2 == 6u) {
                return {.action = A::kNoOp};
            }
            return PerformanceMonitorRegister(a);
        }
        if (a.cp_num == 10u) return VenumRegister(a);
        if (a.cp_num == 14u && !a.read && (a.opc1 == 0u || a.opc1 == 1u) && a.crn == 1u &&
            a.crm == 0u && a.opc2 == 4u) {
            return {.action = A::kNoOp};
        }
        if (a.cp_num == 15u && a.crn == 15u) return ImplementationRegister(a);
        return Armv7aCoprocProviderBase::RegisterTransfer(a);
    }

private:
    ArmCoprocDecision PerformanceMonitorRegister(const ArmCoprocAccess& a) {
        if (a.opc1 != 0u) return {.action = A::kUnimplemented};
        if (a.crm >= 12u && a.crm <= 14u) return Armv7aCoprocProviderBase::RegisterTransfer(a);
        if (a.crm == 15u && a.opc2 == 0u) {
            if (a.read) {
                return {.action = A::kReadCallback, .context = this, .read = &ReadCounterControl};
            }
            return {.action = A::kWriteCallback, .context = this, .write = &WriteCounterControl};
        }
        if (a.crm == 15u && a.opc2 == 2u) return {.action = A::kRegister, .reg = &pmu_c9_c15_2_};
        return {.action = A::kUnimplemented};
    }

    ArmCoprocDecision VenumRegister(const ArmCoprocAccess& a) {
        if (a.opc1 == 7u && a.crn == kVenumRegionRegister && a.crm == 0u && a.opc2 == 0u) {
            if (a.read) {
                return {.action = A::kReadCallback, .context = this, .read = &ReadVenumRegion};
            }
            return {.action = A::kWriteCallback, .context = this, .write = &WriteVenumRegion};
        }
        return {.action = A::kUnimplemented};
    }

    ArmCoprocDecision ImplementationRegister(const ArmCoprocAccess& a) {
        if (a.opc1 == 7u && a.crm == 0u && a.opc2 == 3u) {
            if (!a.read) return {.action = A::kUnimplemented};
            return {.action  = A::kReadCallback,
                    .context = &emu_.Get<IrqController>(),
                    .read    = &IrqController::ReadPendingVectorHelper};
        }
        if (a.opc1 == 7u && a.crm == 1u && a.opc2 == 7u) {
            if (a.read) return {.action = A::kRegister, .reg = &avs_control_};
            return {.action = A::kWriteCallback, .context = this, .write = &WriteAvsControl};
        }
        if (a.opc1 == 7u && a.crm == 0u && a.opc2 == 6u) {
            return {.action = A::kRegister, .reg = &avs_delay_synthesizer_};
        }
        if (a.opc1 < kL2RegionSelection && a.crm == 0u && a.opc2 == 0u) {
            return {.action = A::kRegister, .reg = &region_selection_[a.opc1]};
        }
        if (a.opc1 == kL2RegionSelection && a.crm == 2u && a.opc2 == 0u) {
            return {.action = A::kRegister, .reg = &region_selection_[kL2RegionSelection]};
        }
        return Armv7aCoprocProviderBase::RegisterTransfer(a);
    }

    static uint32_t CERFENGINE_FASTCALL ReadCounterControl(void* self) {
        const auto& p = *static_cast<ScorpionCoprocProvider*>(self);
        return p.counter_control_[p.pmu_->SelectedCounter()];
    }

    static void CERFENGINE_FASTCALL WriteCounterControl(void* self, uint32_t value) {
        auto& p = *static_cast<ScorpionCoprocProvider*>(self);
        p.counter_control_[p.pmu_->SelectedCounter()] = value;
    }

    static uint32_t CERFENGINE_FASTCALL ReadVenumRegion(void* self) {
        auto& p = *static_cast<ScorpionCoprocProvider*>(self);
        p.RequireVenumEnabled();
        return p.venum_region_selection_;
    }

    static void CERFENGINE_FASTCALL WriteVenumRegion(void* self, uint32_t value) {
        auto& p = *static_cast<ScorpionCoprocProvider*>(self);
        p.RequireVenumEnabled();
        p.venum_region_selection_ = value;
    }

    static void CERFENGINE_FASTCALL WriteAvsControl(void* self, uint32_t value) {
        auto& p = *static_cast<ScorpionCoprocProvider*>(self);
        if ((value & ~kAvsModuleEnables) != 0u) {
            p.emu_.Get<Fatal>().Die("Scorpion AVSCSR write 0x%08X sets bits outside the module enables",
                                    value);
        }
        p.avs_control_ = value;
    }

    void RequireVenumEnabled() {
        const uint32_t fpexc =
            emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmCpu>().State()->fpexc;
        if ((fpexc & kFpexcEnable) == 0u) {
            emu_.Get<Fatal>().Die("Scorpion Venum PMRESR access with FPEXC 0x%08X (EN clear)",
                                  fpexc);
        }
    }

    void Reset() {
        region_selection_.fill(0u);
        venum_region_selection_ = 0u;
        counter_control_.fill(0u);
        pmu_c9_c15_2_          = 0u;
        avs_delay_synthesizer_ = 0u;
        avs_control_           = 0u;
    }

    ArmPerformanceMonitors* pmu_ = nullptr;

    std::array<uint32_t, kRegionRegisters>  region_selection_{};
    uint32_t                                venum_region_selection_ = 0;
    std::array<uint32_t, ArmPerformanceMonitors::kMaxEventCounters> counter_control_{};
    uint32_t                                pmu_c9_c15_2_          = 0;
    uint32_t                                avs_delay_synthesizer_ = 0;
    uint32_t                                avs_control_           = 0;
};

}

REGISTER_SERVICE_AS(ScorpionCoprocProvider, ArmCoprocProvider);

}
