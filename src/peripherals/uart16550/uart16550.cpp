#include "uart16550.h"

namespace cerfcore {

void Uart16550::OnReady() {
    AttachLineTimer();
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) { OnLineReset(kind); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Uart16550::OnLineReset(ResetLineKind kind) { LineReset(kind); }

void Uart16550::SaveState(StateWriter& w) {
    Port().SaveCradleState(w);
    Serial16550::SaveState(w);
    SaveLineTimer(w, "line_timer_");
}

void Uart16550::RestoreState(StateReader& r) {
    Port().RestoreCradleState(r);
    Serial16550::RestoreState(r);
    RestoreLineTimer(r, "line_timer_");
}

void Uart16550::PostRestore() {
    ResumeLineTimer();
    PostRestoreLine();
}

void Uart16550::DeliverTx(uint8_t byte) {
    if (Port().DeliverTx(&byte, 1, true)) return;
    EmitDebugTx(byte);
    Port().MarkTx();
}

void Uart16550::EmitDebugTx(uint8_t byte) {
    emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(byte), tx_line_, Name());
}

}
