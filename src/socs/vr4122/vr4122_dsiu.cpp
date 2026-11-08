#include "vr4122_dsiu.h"

#include <cerfcore/boards/board_context.h>
#include "vr4122_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "../vr41xx/vr41xx_icu.h"

namespace cerfcore {

REGISTER_SERVICE(Vr4122Dsiu);

bool Vr4122Dsiu::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Vr4122;
}

void Vr4122Dsiu::OnLineReset(ResetLineKind) {
    Serial16550::Reset();
}

/* DSIUINTREG D11 INTDSIU (VR4131 UM 11.2.3 p193). */
void Vr4122Dsiu::SetInterruptLine(bool pending) {
    emu_.Get<Vr41xxIcu>().SetDsiuSource(pending ? (1u << 11) : 0u);
}

}
