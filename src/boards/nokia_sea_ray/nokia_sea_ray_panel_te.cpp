#include "nokia_sea_ray_panel_te.h"

#include "../../socs/msm8255/msm8255_gpio_bus.h"
#include "nokia_sea_ray_family_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/hibernation.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint32_t kTePin        = 30u;
constexpr uint64_t kLineHz       = 52080u;
constexpr uint64_t kActiveLines  = 864u;
constexpr uint64_t kFrameLines   = 868u;
constexpr uint64_t kTeHighLines  = kFrameLines - kActiveLines;

}

REGISTER_SERVICE(NokiaSeaRayPanelTe);

bool NokiaSeaRayPanelTe::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
}

void NokiaSeaRayPanelTe::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    lines_.Attach(kLineHz, 1u);
    edge_ = clock_->Add([this] { Plan(false); });
    clock_->RegisterRateListener([this] {
        lines_.Rescale();
        Plan(false);
    });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { Plan(true); });
    PowerOn();
}

void NokiaSeaRayPanelTe::PowerOn() {
    scanning_  = false;
    tear_on_   = false;
    tear_line_ = 0u;
    Plan(false);
}

void NokiaSeaRayPanelTe::SetScanning(bool scanning) {
    if (scanning == scanning_) return;
    scanning_ = scanning;
    if (scanning) scan_anchor_ = lines_.Now();
    Plan(false);
}

void NokiaSeaRayPanelTe::SetTearLine(uint32_t line) {
    tear_on_   = true;
    tear_line_ = line;
    Plan(false);
}

void NokiaSeaRayPanelTe::SetTearOff() {
    tear_on_ = false;
    Plan(false);
}

void NokiaSeaRayPanelTe::Plan(bool restoring) {
    clock_->Disarm(edge_);
    auto& bus = emu_.Get<Msm8255GpioBus>();
    if (!Active() || tear_line_ >= kFrameLines) {
        restoring ? bus.RestoreInputPin(kTePin, false) : bus.SetInputPin(kTePin, false);
        return;
    }
    const uint64_t now   = lines_.Now();
    const uint64_t line  = (now - scan_anchor_) % kFrameLines;
    const uint64_t phase = (line + kFrameLines - tear_line_) % kFrameLines;
    const bool     high  = phase < kTeHighLines;
    restoring ? bus.RestoreInputPin(kTePin, high) : bus.SetInputPin(kTePin, high);
    lines_.ArmAt(edge_, now + (high ? kTeHighLines - phase : kFrameLines - phase));
}

void NokiaSeaRayPanelTe::SaveState(StateWriter& w) {
    lines_.Save(w, "te_lines_");
    w.Write<uint32_t>("te_scanning", scanning_ ? 1u : 0u);
    w.Write<uint32_t>("te_on", tear_on_ ? 1u : 0u);
    w.Write<uint32_t>("te_line", tear_line_);
    w.Write<uint64_t>("te_scan_anchor", scan_anchor_);
}

void NokiaSeaRayPanelTe::RestoreState(StateReader& r) {
    uint32_t scanning = 0u;
    uint32_t on       = 0u;
    lines_.Restore(r, "te_lines_");
    r.Read("te_scanning", scanning);
    r.Read("te_on", on);
    r.Read("te_line", tear_line_);
    r.Read("te_scan_anchor", scan_anchor_);
    scanning_ = scanning != 0u;
    tear_on_  = on != 0u;
}

}
