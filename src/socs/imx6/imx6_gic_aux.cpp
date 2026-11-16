#include "imx6_gic_aux.h"
#include "imx6_gic_identity.h"

#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace imx6_gic_detail {

namespace {

constexpr uint32_t kScuConfigTagRam32Kb = 0x00000100u;
constexpr uint32_t kScuConfigCpu0Smp = 0x00000010u;
constexpr uint32_t kActlrSmp = 0x00000040u;
constexpr uint32_t kScuConfigCpuNumber = imx6_gic_detail::kCortexA9CoreCount - 1u;

constexpr uint32_t kScuControlAddressFiltering = 0x00000002u;
constexpr uint32_t kScuAccessControlCpu0 = 0x00000001u;

}

bool Imx6GicAux::ScuRead(uint32_t off, uint32_t aux_control_register, uint32_t& value) const {
    switch (off) {
    case 0x000: value = scu_control_; return true;
    case 0x004:
        value = kScuConfigTagRam32Kb | kScuConfigCpuNumber |
                ((aux_control_register & kActlrSmp) != 0u ? kScuConfigCpu0Smp : 0u);
        return true;
    case 0x030: value = 0u; return true;
    case 0x050: value = scu_access_control_; return true;
    default: return false;
    }
}

bool Imx6GicAux::ScuWrite(uint32_t off, uint32_t value, const char*& unmodelled) {
    switch (off) {
    case 0x000:
        if ((value & kScuControlAddressFiltering) != 0u) unmodelled = "SCU address filtering";
        scu_control_ = value;
        return true;
    case 0x00C:
    case 0x030: return true;
    case 0x050:
        if ((value & kScuAccessControlCpu0) == 0u) unmodelled = "an SCU access control that locks CPU0 out";
        scu_access_control_ = value;
        return true;
    default: return false;
    }
}

void Imx6GicAux::Reset() { *this = Imx6GicAux{}; }

void Imx6GicAux::SaveState(StateWriter& w) const {
    w.Write("scu_control", scu_control_);
    w.Write("scu_access_control", scu_access_control_);
}

void Imx6GicAux::RestoreState(StateReader& r) {
    r.Read("scu_control", scu_control_);
    r.Read("scu_access_control", scu_access_control_);
}

}

}
