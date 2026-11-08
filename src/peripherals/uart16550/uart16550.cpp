#include "uart16550.h"

namespace cerfcore {

void Uart16550::OnReady() {
    device_clock_ = &emu_.Get<ExternalDeviceClock>();
    line_timer_   = &device_clock_->AddTimer([this] { OnLineTimer(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) { OnLineReset(kind); });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

void Uart16550::OnLineReset(ResetLineKind kind) { LineReset(kind); }

void Uart16550::SaveState(StateWriter& w) {
    Serial16550::SaveState(w);
    device_clock_->SaveTimer(*line_timer_, w, "line_timer_");
}

void Uart16550::RestoreState(StateReader& r) {
    Serial16550::RestoreState(r);
    device_clock_->RestoreTimer(*line_timer_, r, "line_timer_");
}

void Uart16550::PostRestore() {
    device_clock_->ResumeTimer(*line_timer_);
    PostRestoreLine();
}

void Uart16550::ArmLineTimer(int64_t delay_ns) { device_clock_->Arm(*line_timer_, delay_ns); }

void Uart16550::DisarmLineTimer() { device_clock_->Disarm(*line_timer_); }

bool Uart16550::DeliverTx(uint8_t byte) {
    if (DeliverTxToEndpoint(byte)) return true;
    EmitDebugTx(byte);
    return true;
}

void Uart16550::EmitDebugTx(uint8_t byte) {
    emu_.Get<KernelDebugSink>().EmitChar(static_cast<char>(byte), tx_line_, Name());
}

}
