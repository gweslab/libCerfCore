#include "../reg_window_impl.h"
#include "vr4122_id.h"

#include <cstdint>

namespace cerfcore {

namespace {

using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegWindowBase;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

constexpr uint32_t kOffFrstr = 0x00u;
constexpr uint32_t kOffIrsr1 = 0x18u;

constexpr RegWindowModel Vr4122FirModel() {
    RegWindowModel m{0x0F000840u, kOffIrsr1 + 2u, kOffIrsr1 / 2u + 1u, false, {}};
    m.reg[kOffFrstr / 2u] = {ReadKind::kZero, WriteKind::kFatal};
    m.reg[kOffIrsr1 / 2u] = {ReadKind::kZero, WriteKind::kDrop};
    return m;
}

constexpr RegWindowModel kVr4122FirModel = Vr4122FirModel();

class Vr4122Fir : public RegWindowBase<SocId::Vr4122, kVr4122FirModel> {
public:
    using RegWindowBase::RegWindowBase;

protected:
    const char* RegStateName(uint32_t i) const override {
        (void)i;
        return "fir_reg";
    }
};

}

REGISTER_SERVICE(Vr4122Fir);

}
