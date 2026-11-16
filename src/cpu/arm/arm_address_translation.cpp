#include "arm_address_translation.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "../../execution/engine_bridge.h"
#include "arm_cp15_registers.h"
#include "arm_page_walker.h"
#include "arm_par_attributes.h"
#include <cerfengine/arm/arm_mmu.h>
#include <cerfengine/arm/arm_system_host.h>

namespace cerfcore {

REGISTER_SERVICE(ArmAddressTranslation);

namespace {

constexpr uint32_t kParAttributeStub = 0u;

}

bool ArmAddressTranslation::ShouldRegister() {
    return emu_.Get<BoardContext>().GetCpuArch() == CpuArch::kArm;
}

void ArmAddressTranslation::OnReady() {
    cp15_             = &emu_.Get<ArmCp15Registers>();
    walker_           = &emu_.Get<ArmPageWalker>();
    bridge_           = &emu_.Get<EngineBridge>();
}

void CERFENGINE_FASTCALL ArmAddressTranslation::Translate(void* self, uint32_t opc2, uint32_t va) {
    auto& at = *static_cast<ArmAddressTranslation*>(self);
    const uint32_t par = at.Par(opc2, va);
    at.cp15_->File().par = par;
}

uint32_t ArmAddressTranslation::Par(uint32_t opc2, uint32_t va) {
    using cerfengine::ArmMmuAccess;
    const cerfengine::ArmMmuState& state = *bridge_->Engine().Get<cerfengine::ArmMmu>().State();
    const uint32_t control = state.effective_control_register.word;
    const uint32_t p       = cerfengine::ArmFcseFold(va, state.fcse_fold_id);

    if (!state.effective_control_register.bits.m) {
        return (p & 0xFFFFF000u) | ArmMmuDisabledDataParAttributes();
    }

    ArmCp15File& file = cp15_->File();
    const uint32_t saved_dfsr = file.dfsr;
    const uint32_t saved_dfar = file.dfar;

    const bool user = opc2 >= 2u;
    cerfengine::ArmTranslation t;
    switch (opc2) {
    case 0u: t = cerfengine::host::ArmWalk<ArmMmuAccess::kRead>(walker_, p, user, control); break;
    case 1u: t = cerfengine::host::ArmWalk<ArmMmuAccess::kReadWrite>(walker_, p, user, control); break;
    case 2u: t = cerfengine::host::ArmWalk<ArmMmuAccess::kRead>(walker_, p, user, control); break;
    default: t = cerfengine::host::ArmWalk<ArmMmuAccess::kWrite>(walker_, p, user, control); break;
    }

    uint32_t par = 0u;
    if (t.mapped) {
        par = (t.pa & 0xFFFFF000u) | kParAttributeStub;
    } else {
        ArmDfsr fsr{};
        fsr.word = file.dfsr;
        const uint32_t fs = fsr.bits.status | (fsr.bits.fs4 << 4);
        par = 1u | (fs << 1);
    }

    file.dfsr = saved_dfsr;
    file.dfar = saved_dfar;
    return par;
}

}
