#include "serial_endpoint_port.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/host/host_widget.h>
#include <cerfcore/host/host_widget_registry.h>
#include "serial_cradle.h"
#include "serial_endpoint.h"

#include <utility>

namespace cerfcore {

SerialEndpointPort::SerialEndpointPort(SerialLine& line, SerialLineTimer& timer, ControlFn control)
    : line_(line), timer_(timer), control_(std::move(control)) {}

SerialEndpointPort::~SerialEndpointPort() = default;

void SerialEndpointPort::AttachCradle(CerfEmulator& emu, std::string label) {
    cradle_   = std::make_unique<SerialCradle>(emu, line_, std::move(label));
    activity_ = cradle_.get();
    emu.Get<HostWidgetRegistry>().Register(cradle_.get());
}

void SerialEndpointPort::OnShutdown() {
    if (cradle_) cradle_->OnShutdown();
}

void SerialEndpointPort::SetEndpoint(SerialEndpoint* ep) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    endpoint_    = ep;
    lines_known_ = false;
    NotifyControlLines();
}

void SerialEndpointPort::NotifyControlLines() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (endpoint_ == nullptr) return;
    const SerialControlLines lines = control_();
    if (lines_known_ && lines.dtr == sent_dtr_ && lines.rts == sent_rts_) return;
    lines_known_ = true;
    sent_dtr_    = lines.dtr;
    sent_rts_    = lines.rts;
    endpoint_->OnControlLines(lines.dtr, lines.rts);
}

void SerialEndpointPort::ResendModemInputs() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (endpoint_ != nullptr) endpoint_->ResendModemInputs();
}

bool SerialEndpointPort::DeliverTx(const uint8_t* data, size_t n, bool wire_connected) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (endpoint_ == nullptr) return false;
    if (!wire_connected) return true;
    endpoint_->OnGuestTx(data, n);
    MarkTx();
    return true;
}

void SerialEndpointPort::MarkTx() {
    if (activity_ != nullptr) activity_->MarkTx();
}

void SerialEndpointPort::MarkRx() {
    if (activity_ != nullptr) activity_->MarkRx();
}

void SerialEndpointPort::SetRxDrainCallback(SerialLine::RxDrainFn cb) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    rx_drain_ = std::move(cb);
}

void SerialEndpointPort::NotifyRxDrained() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (rx_drain_) rx_drain_();
}

void SerialEndpointPort::SetLineConfigCallback(SerialLine::LineConfigFn cb) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    line_cfg_cb_ = std::move(cb);
}

void SerialEndpointPort::NotifyLineConfig(const SerialLine::LineConfig& cfg) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (line_cfg_cb_) line_cfg_cb_(cfg);
}

void SerialEndpointPort::ArmTimer(int64_t delay_ns) { timer_.ArmLineTimer(delay_ns); }

void SerialEndpointPort::DisarmTimer() { timer_.DisarmLineTimer(); }

void SerialEndpointPort::OnTimer() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (endpoint_ != nullptr) endpoint_->OnEndpointTimer();
}

void SerialEndpointPort::SaveCradleState(StateWriter& w) {
    if (cradle_) cradle_->SaveCradleState(w);
}

void SerialEndpointPort::RestoreCradleState(StateReader& r) {
    if (cradle_) cradle_->RestoreCradleState(r);
}

void SerialEndpointPort::SaveEndpointState(StateWriter& w) const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (endpoint_ != nullptr) endpoint_->SaveState(w);
}

void SerialEndpointPort::RestoreEndpointState(StateReader& r) {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    lines_known_ = false;
    if (endpoint_ != nullptr) endpoint_->RestoreState(r);
}

void SerialEndpointPort::PostRestore() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (endpoint_ != nullptr) endpoint_->PostRestore();
    lines_known_ = false;
    NotifyControlLines();
}

}
