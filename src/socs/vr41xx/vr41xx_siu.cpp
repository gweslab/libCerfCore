#include "vr41xx_siu.h"

#include <cerfcore/host/guest_deep_sleep.h>

namespace cerfcore {

void Vr41xxSiu::OnLineReset(ResetLineKind) {
    hibernating_.store(false, std::memory_order_release);
    Serial16550::ResetRegisters();
    irsel_.store(0u, std::memory_order_release);
    ResetChip();
}

void Vr41xxSiu::OnReady() {
    Uart16550::OnReady();

    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        RefreshClocks();
        ResendEndpointInputs();
    });
    TrackClock(kCmuMskSiu);
    TrackClock(kCmuMskSsiu);
    RefreshClocks();

    wiring_ = emu_.TryGet<Vr41xxSerialWiring>();
    if (!wiring_) return;
    modem_ = wiring_->ForSiu();
    if (!modem_) return;

    {
        std::lock_guard<std::mutex> lk(modem_mtx_);
        DriveCarrierPin(false);
        LoadModemInputs(false, false, false, DcdLevel());
    }
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] { ApplyModemInputs(); });
    wiring_->RegisterSupplyListener([this] {
        ApplyModemInputs();
        RefreshControlLines();
    });
    emu_.Get<GuestDeepSleep>().RegisterSleepEntryListener([this] {
        hibernating_.store(true, std::memory_order_release);
        RefreshControlLines();
    });

    cradle_ = std::make_unique<SerialCradle>(emu_, *this, modem_->label);
    SetActivityWidget(cradle_.get());
    emu_.Get<HostWidgetRegistry>().Register(cradle_.get());
    LOG(Periph, "[UART] SIU attached as serial endpoint line '%s'\n", modem_->label.c_str());
}

void Vr41xxSiu::SetModemInputs(bool cts, bool dsr, bool, bool dcd) {
    std::lock_guard<std::mutex> lk(modem_mtx_);
    connector_cts_ = cts;
    connector_dsr_ = dsr;
    DriveCarrierPin(dcd);
    ApplyModemInputsLocked();
}

void Vr41xxSiu::PushRx(const uint8_t* data, size_t n) {
    if (Rs232Selected()) Uart16550::PushRx(data, n);
}

bool Vr41xxSiu::DeliverTx(uint8_t byte) {
    if (Rs232Selected()) return Uart16550::DeliverTx(byte);
    if (TransmitsOnTxd()) EmitDebugTx(byte);
    return false;
}

bool Vr41xxSiu::TransmitsOnTxd() {
    return (irsel_.load(std::memory_order_acquire) & kIrselSirsel) == 0u &&
           clocks_.load(std::memory_order_acquire) == kSiuClocks;
}

bool Vr41xxSiu::ControlOutputsDriven() { return Rs232PinsLive(); }

uint32_t Vr41xxSiu::ReadExtReg(uint32_t idx) {
    if (idx == kIrselIndex) return irsel_.load(std::memory_order_acquire);
    return ReadChipExtReg(idx);
}

void Vr41xxSiu::WriteExtReg(uint32_t idx, uint32_t value) {
    if (idx == kIrselIndex) {
        irsel_.store(static_cast<uint8_t>(value & kIrselWritable), std::memory_order_release);
        return;
    }
    WriteChipExtReg(idx, value);
}

void Vr41xxSiu::SaveState(StateWriter& w) {
    if (cradle_) cradle_->SaveCradleState(w);
    Uart16550::SaveState(w);
    const uint8_t irsel = irsel_.load(std::memory_order_acquire);
    w.Write("irsel", irsel);
    w.Write<uint8_t>("hibernating", hibernating_.load(std::memory_order_acquire) ? 1u : 0u);
    SaveChipState(w);
}

void Vr41xxSiu::RestoreState(StateReader& r) {
    if (cradle_) cradle_->RestoreCradleState(r);
    Uart16550::RestoreState(r);
    uint8_t irsel = 0u;
    r.Read("irsel", irsel);
    irsel_.store(irsel, std::memory_order_release);
    uint8_t hibernating = 0u;
    r.Read("hibernating", hibernating);
    hibernating_.store(hibernating != 0u, std::memory_order_release);
    RestoreChipState(r);
}

void Vr41xxSiu::PostRestore() {
    RefreshClocks();
    Uart16550::PostRestore();
}

void Vr41xxSiu::TrackClock(uint16_t mask_bit) {
    emu_.Get<Vr41xxCmu>().RegisterClockListener(mask_bit, [this, mask_bit](bool supplied_after) {
        if (supplied_after) {
            clocks_.fetch_or(mask_bit, std::memory_order_acq_rel);
        } else {
            clocks_.fetch_and(static_cast<uint16_t>(~mask_bit), std::memory_order_acq_rel);
        }
    });
}

void Vr41xxSiu::RefreshClocks() {
    clocks_.store(emu_.Get<Vr41xxCmu>().SuppliedClocks(kSiuClocks), std::memory_order_release);
}

void Vr41xxSiu::DriveCarrierPin(bool carrier) {
    emu_.Get<Vr41xxGiu>().SetPinLevel(modem_->carrier_gpio, !carrier);
}

bool Vr41xxSiu::DcdLevel() {
    return !emu_.Get<Vr41xxGiu>().GetPinLevel(Vr41xxGiu::kDcdPin);
}

bool Vr41xxSiu::SupplyOn() { return wiring_->SupplyOn(); }

bool Vr41xxSiu::Rs232PinsLive() {
    if (!modem_) return true;
    return SupplyOn() && !hibernating_.load(std::memory_order_acquire);
}

bool Vr41xxSiu::Rs232Selected() { return TransmitsOnTxd() && Rs232PinsLive(); }

void Vr41xxSiu::ApplyModemInputs() {
    std::lock_guard<std::mutex> lk(modem_mtx_);
    ApplyModemInputsLocked();
}

void Vr41xxSiu::ApplyModemInputsLocked() {
    const bool powered = SupplyOn();
    Uart16550::SetModemInputs(powered && connector_cts_, powered && connector_dsr_, false,
                              DcdLevel());
}

}
