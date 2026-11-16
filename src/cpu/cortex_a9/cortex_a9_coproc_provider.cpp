#include "../armv7a_coproc_provider_base.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../socs/imx6/imx6_id.h"
#include "../arm/arm_cp15_registers.h"
#include "../arm_processor_config.h"

namespace cerfcore {

namespace {

using cerfengine::ArmCoprocAccess;
using cerfengine::ArmCoprocDecision;
using A = cerfengine::ArmCoprocAction;

constexpr uint32_t kDiagnosticControlGuestWrittenMask = 0x01701850u;

class CortexA9CoprocProvider : public Armv7aCoprocProviderBase {
public:
    using Armv7aCoprocProviderBase::Armv7aCoprocProviderBase;

    bool ShouldRegister() override {
        return SocId::IsImx6(emu_.Get<BoardContext>().GetSocId());
    }

    void OnReady() override {
        Armv7aCoprocProviderBase::OnReady();
        regs_ = &emu_.Get<ArmCp15Registers>();
        config_ = &emu_.Get<ArmProcessorConfig>();
    }

protected:
    ArmCoprocDecision RegisterTransfer(const ArmCoprocAccess& a) override {
        if (a.cp_num == 15u) {
            if (a.read && a.opc1 == 4u && a.crn == 15u && a.crm == 0u && a.opc2 == 0u) {
                return {.action = A::kConstant, .constant = config_->Periphbase()};
            }
            if (a.read && a.opc1 == 1u && a.crn == 0u && a.crm == 0u && a.opc2 == 7u) {
                return {.action = A::kConstant, .constant = 0u};
            }
            if (a.read && a.opc1 == 0u && a.crn == 0u) {
                uint32_t value = 0;
                if (IdentificationValue(a, value)) return {.action = A::kConstant, .constant = value};
            }
            if (a.opc1 == 0u && a.crn == 15u && a.crm == 0u && a.opc2 == 1u) {
                if (a.read)
                    return {.action = A::kRegister, .reg = &regs_->File().cortex_a9_diagnostic_control};
                return {.action = A::kWriteCallback, .context = this,
                        .write = [](void* context, uint32_t value) { WriteDiagnosticControl(context, value); }};
            }
        }
        return Armv7aCoprocProviderBase::RegisterTransfer(a);
    }

private:
    static void CERFENGINE_FASTCALL WriteDiagnosticControl(void* context, uint32_t value) {
        auto* self = static_cast<CortexA9CoprocProvider*>(context);
        uint32_t& diagnostic = self->regs_->File().cortex_a9_diagnostic_control;
        const uint32_t changed = diagnostic ^ value;
        const uint32_t unsupported = changed & ~kDiagnosticControlGuestWrittenMask;
        if (unsupported != 0u)
            self->emu_.Get<Fatal>().Die("Cortex-A9 Diagnostic Control bits 0x%08X have no modeled effect",
                                        unsupported);
        diagnostic = value & kDiagnosticControlGuestWrittenMask;
    }

    bool IdentificationValue(const ArmCoprocAccess& a, uint32_t& value) const {
        if (a.crm == 2u && a.opc1 == 0u && (a.opc2 == 6u || a.opc2 == 7u)) {
            value = 0u;
            return true;
        }
        if (a.crm == 0u && a.opc1 == 0u) {
            switch (a.opc2) {
            case 2u: value = config_->Tcmtr(); return true;
            case 3u: value = config_->Tlbtr(); return true;
            case 5u: value = config_->Mpidr(); return true;
            case 4u:
            case 6u:
            case 7u: value = config_->Midr(); return true;
            default: return false;
            }
        }
        if (a.opc1 == 0u && a.crm >= 3u && a.crm <= 7u && a.opc2 <= 7u) {
            value = 0u;
            return true;
        }
        if (a.crm == 1u) {
            switch (a.opc2) {
            case 0u: value = config_->IdPfr0(); return true;
            case 1u: value = config_->IdPfr1(); return true;
            case 2u: value = config_->IdDfr0(); return true;
            case 3u: value = config_->IdAfr0(); return true;
            case 4u: value = config_->IdMmfr0(); return true;
            case 5u: value = config_->IdMmfr1(); return true;
            case 6u: value = config_->IdMmfr2(); return true;
            case 7u: value = config_->IdMmfr3(); return true;
            default: return false;
            }
        }
        if (a.crm == 2u) {
            switch (a.opc2) {
            case 0u: value = config_->IdIsar0(); return true;
            case 1u: value = config_->IdIsar1(); return true;
            case 2u: value = config_->IdIsar2(); return true;
            case 3u: value = config_->IdIsar3(); return true;
            case 4u: value = config_->IdIsar4(); return true;
            case 5u: value = config_->IdIsar5(); return true;
            default: return false;
        }
        }
        return false;
    }

    ArmCp15Registers* regs_ = nullptr;
    ArmProcessorConfig* config_ = nullptr;
};

}

REGISTER_SERVICE_AS(CortexA9CoprocProvider, ArmCoprocProvider);

}
