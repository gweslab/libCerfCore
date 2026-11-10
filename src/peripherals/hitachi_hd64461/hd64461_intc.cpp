#include "hd64461_intc.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../boards/hp_600/hp_600_family_id.h"
#include "hd64461_irq_output.h"

namespace cerfcore {

REGISTER_SERVICE(Hd64461Intc);

namespace {

constexpr uint32_t kNimrIndex = 1u;

}

bool Hd64461Intc::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::Hp600;
}

void Hd64461Intc::OnReady() {
    output_ = &emu_.Get<Hd64461IrqOutput>();
    RegWindow::OnReady();
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        std::lock_guard<std::mutex> lk(lock_);
        DriveLocked();
    });
}

uint16_t Hd64461Intc::SampleRegister(uint32_t i, uint32_t addr) {
    (void)i;
    (void)addr;
    return requests_;
}

void Hd64461Intc::AfterStore(uint32_t i) {
    (void)i;
    DriveLocked();
}

void Hd64461Intc::SetSourceRequest(Hd64461IntcSource source, bool requesting) {
    std::lock_guard<std::mutex> lk(lock_);
    const uint16_t bit = static_cast<uint16_t>(1u << static_cast<uint32_t>(source));
    requests_          = static_cast<uint16_t>(requesting ? (requests_ | bit) : (requests_ & ~bit));
    DriveLocked();
}

void Hd64461Intc::DriveLocked() { output_->SetIrq0Asserted((requests_ & ~StoredReg(kNimrIndex)) != 0u); }

void Hd64461Intc::SaveState(StateWriter& w) {
    RegWindow::SaveState(w);
    std::lock_guard<std::mutex> lk(lock_);
    w.Write("requests", requests_);
}

void Hd64461Intc::RestoreState(StateReader& r) {
    RegWindow::RestoreState(r);
    std::lock_guard<std::mutex> lk(lock_);
    r.Read("requests", requests_);
}

void Hd64461Intc::PostRestore() {
    std::lock_guard<std::mutex> lk(lock_);
    DriveLocked();
}

}
