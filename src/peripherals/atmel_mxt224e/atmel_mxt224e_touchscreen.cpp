#include "atmel_mxt224e_touchscreen.h"

#include "../../core/board_parts.h"
#include "atmel_mxt224e.h"
#include "atmel_mxt224e_display_area.h"
#include "atmel_mxt224e_part_id.h"
#include <cerfcore/core/cerf_emulator.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint16_t kMxtT7ActvAcqIntToIdle = kMxtT7Start + 2u;

constexpr uint16_t kT9Tchdi     = kMxtT9Start + 8u;
constexpr uint16_t kT9Orient    = kMxtT9Start + 9u;
constexpr uint16_t kT9MovHystI  = kMxtT9Start + 11u;
constexpr uint16_t kT9MovHystN  = kMxtT9Start + 12u;
constexpr uint16_t kT9YEdgeCtrl = kMxtT9Start + 28u;
constexpr uint16_t kT9JumpLimit = kMxtT9Start + 30u;
constexpr uint16_t kT9NextTchdi = kMxtT9Start + 34u;

constexpr uint8_t kDisMove = 0x10u;
constexpr uint8_t kDisRel  = 0x20u;
constexpr uint8_t kDisPrss = 0x40u;

constexpr uint8_t kSwitch  = 0x01u;
constexpr uint8_t kInvertX = 0x02u;
constexpr uint8_t kInvertY = 0x04u;

constexpr uint8_t kRelUpdate = 0x40u;

constexpr uint8_t kPress   = 0x40u;
constexpr uint8_t kRelease = 0x20u;
constexpr uint8_t kMove    = 0x10u;

constexpr uint8_t kDeepSleep = 0u;
constexpr uint8_t kFreeRun   = 255u;

constexpr int64_t  kNsPerMs           = 1'000'000;
constexpr int64_t  kActiveToIdleStep  = 200 * kNsPerMs;
constexpr int64_t  kFreeRunNoTouchNs  = 1'000'000'000 / 415;
constexpr int64_t  kFreeRunOneTouchNs = 1'000'000'000 / 198;
constexpr uint8_t  kTouchArea         = 1u;
constexpr uint16_t kIntegrationMax    = 255u;
constexpr uint16_t kReleaseCyclesMin  = 3u;
constexpr int64_t  kJumpUnit          = 8;

constexpr uint32_t kPlateMax      = 0xFFFFu;

constexpr int64_t kTapHoldNs     = 100 * kNsPerMs;
constexpr uint8_t kTouchSpaceTap = HostPenInput::kFirstOwnerKind;

uint32_t PlateShare(uint32_t part, uint32_t whole) { return kPlateMax * part / whole; }

uint16_t Scale(uint32_t plate, uint16_t range) {
    return static_cast<uint16_t>((plate * range + kPlateMax / 2u) / kPlateMax);
}

int64_t SquaredDistance(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by) {
    const int64_t dx = static_cast<int64_t>(ax) - bx;
    const int64_t dy = static_cast<int64_t>(ay) - by;
    return dx * dx + dy * dy;
}

}

bool AtmelMxt224eTouchscreen::ShouldRegister() { return emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e); }

void AtmelMxt224eTouchscreen::OnReady() {
    auto& clock = emu_.Get<GuestCycleClock>();
    event_      = clock.Add([this] { Acquire(); });
    clock.RegisterRateListener([this] {
        if (armed_) Arm(due_ns_);
    });
}

void AtmelMxt224eTouchscreen::ApplyPen(const HostPenInput::Pen& pen) {
    if (pen.kind != kTouchSpaceTap) {
        host_down_  = pen.kind == HostPenInput::kPenDown;
        tap_active_ = false;
        Schedule();
        return;
    }
    const auto area = emu_.Get<AtmelMxt224eDisplayArea>().DisplayArea();
    host_down_      = false;
    tap_plate_x_    = PlateShare(static_cast<uint32_t>(pen.x), area.touch_width);
    tap_plate_y_    = PlateShare(static_cast<uint32_t>(pen.y), area.touch_height);
    tap_active_     = true;
    tap_until_ns_   = emu_.Get<GuestCycleClock>().NowNs() + kTapHoldNs;
    Schedule();
}

void AtmelMxt224eTouchscreen::ResetPen() {
    host_down_  = false;
    tap_active_ = false;
    Schedule();
}

void AtmelMxt224eTouchscreen::TapTouchSpace(uint16_t x, uint16_t y) { QueuePen(HostPenInput::Pen{kTouchSpaceTap, x, y}); }

bool AtmelMxt224eTouchscreen::Down() const { return host_down_ || tap_active_; }

void AtmelMxt224eTouchscreen::ResetTouch() {
    if (armed_) emu_.Get<GuestCycleClock>().Disarm(event_);
    armed_ = false;
    touches_.fill(Touch{});
    tracked_         = -1;
    active_until_ns_ = 0;
    Schedule();
}

void AtmelMxt224eTouchscreen::ConfigChanged() { Schedule(); }

void AtmelMxt224eTouchscreen::ReportAll() {
    auto& chip = emu_.Get<AtmelMxt224e>();
    if (!chip.TouchscreenScanning() || (chip.Config(kMxtT9Ctrl) & kMxtT9Rpten) == 0u) return;
    const uint8_t touches = chip.Config(kMxtT9NumTouch);
    for (uint8_t slot = 0u; slot < touches; ++slot) {
        const Touch& t = touches_[slot];
        if (t.state == TouchState::kDetected) {
            Report(chip, slot, kMxtT9Detect, {t.report_x, t.report_y});
        } else {
            Report(chip, slot, 0u, {});
        }
    }
}

bool AtmelMxt224eTouchscreen::Sleeping() {
    const auto& chip = emu_.Get<AtmelMxt224e>();
    if (FastDetectIntegration(chip)) return false;
    return chip.Config(ActiveMode() ? kMxtT7ActvAcqInt : kMxtT7IdleAcqInt) == kDeepSleep;
}

void AtmelMxt224eTouchscreen::Schedule() {
    if (armed_ || (!Down() && !Busy())) return;
    const int64_t interval = IntervalNs(emu_.Get<AtmelMxt224e>());
    if (interval == 0) return;
    Arm(emu_.Get<GuestCycleClock>().NowNs() + interval);
}

void AtmelMxt224eTouchscreen::Arm(int64_t due_ns) {
    auto& clock = emu_.Get<GuestCycleClock>();
    due_ns_     = due_ns;
    armed_      = true;
    clock.Arm(event_, clock.CycleAtNs(due_ns));
}

bool AtmelMxt224eTouchscreen::Busy() const {
    return std::any_of(touches_.begin(), touches_.end(),
                       [](const Touch& t) { return t.state != TouchState::kFree; });
}

bool AtmelMxt224eTouchscreen::FastDetectIntegration(const AtmelMxt224e& chip) const {
    const uint16_t release = ReleaseCycles(chip);
    return std::any_of(touches_.begin(), touches_.end(), [release](const Touch& t) {
        return t.state == TouchState::kDetecting || (t.state == TouchState::kDetected && t.count < release);
    });
}

bool AtmelMxt224eTouchscreen::ActiveMode() {
    const bool detected = std::any_of(touches_.begin(), touches_.end(),
                                      [](const Touch& t) { return t.state == TouchState::kDetected; });
    return detected || emu_.Get<GuestCycleClock>().NowNs() < active_until_ns_;
}

int64_t AtmelMxt224eTouchscreen::IntervalNs(const AtmelMxt224e& chip) {
    if (!chip.TouchscreenScanning()) return 0;
    const int64_t free_run = Down() ? kFreeRunOneTouchNs : kFreeRunNoTouchNs;
    if (FastDetectIntegration(chip)) return free_run;
    const uint8_t interval = chip.Config(ActiveMode() ? kMxtT7ActvAcqInt : kMxtT7IdleAcqInt);
    if (interval == kDeepSleep) return 0;
    if (interval == kFreeRun) return free_run;
    return interval * kNsPerMs;
}

AtmelMxt224eTouchscreen::Position AtmelMxt224eTouchscreen::Locate(const AtmelMxt224e& chip) const {
    uint32_t surface_x = tap_plate_x_;
    uint32_t surface_y = tap_plate_y_;
    if (!tap_active_) {
        const auto area = emu_.Get<AtmelMxt224eDisplayArea>().DisplayArea();
        const HostPenInput::PanelRange display = {
            0u, static_cast<uint16_t>(PlateShare(area.display_width, area.touch_width)),
            0u, static_cast<uint16_t>(PlateShare(area.display_height, area.touch_height))};
        surface_x = PlatePosition(display, Plate::kX);
        surface_y = PlatePosition(display, Plate::kY);
    }
    const bool follow_y = chip.XLinesFollowSurfaceY();
    return {Scale(follow_y ? surface_y : surface_x, chip.ConfigRange(kMxtT9XRange)),
            Scale(follow_y ? surface_x : surface_y, chip.ConfigRange(kMxtT9YRange))};
}

bool AtmelMxt224eTouchscreen::Jumped(const Touch& t, const Position& p, const AtmelMxt224e& chip) const {
    const int64_t limit = chip.Config(kT9JumpLimit) * kJumpUnit;
    if (limit == 0) return false;
    return SquaredDistance(p.x, p.y, t.raw_x, t.raw_y) > limit * limit;
}

int AtmelMxt224eTouchscreen::Continued(const Position& p, const AtmelMxt224e& chip) const {
    const uint16_t release = ReleaseCycles(chip);
    int            best    = -1;
    int64_t        nearest = 0;
    for (uint8_t slot = 0u; slot < touches_.size(); ++slot) {
        const Touch& t = touches_[slot];
        if (t.state != TouchState::kDetected || t.count >= release || Jumped(t, p, chip)) continue;
        const int64_t d = SquaredDistance(p.x, p.y, t.raw_x, t.raw_y);
        if (best < 0 || d < nearest) {
            best    = slot;
            nearest = d;
        }
    }
    return best;
}

int AtmelMxt224eTouchscreen::FreeSlot(const AtmelMxt224e& chip) const {
    const uint8_t touches = chip.Config(kMxtT9NumTouch);
    for (uint8_t slot = 0u; slot < touches; ++slot) {
        if (touches_[slot].state == TouchState::kFree) return slot;
    }
    return -1;
}

uint16_t AtmelMxt224eTouchscreen::DetectCycles(const AtmelMxt224e& chip, uint8_t slot) const {
    uint16_t limit = std::max<uint16_t>(chip.Config(kT9Tchdi), 1u);
    for (uint8_t i = 0u; i < touches_.size(); ++i) {
        if (i != slot && touches_[i].state == TouchState::kDetected) {
            limit = std::min<uint16_t>(limit + chip.Config(kT9NextTchdi), kIntegrationMax);
            break;
        }
    }
    return static_cast<uint16_t>(limit + 1u);
}

uint16_t AtmelMxt224eTouchscreen::ReleaseCycles(const AtmelMxt224e& chip) const {
    const uint16_t tchdi = std::max<uint16_t>(chip.Config(kT9Tchdi), 1u);
    const uint16_t limit = std::min<uint16_t>(tchdi + chip.Config(kT9NextTchdi), kIntegrationMax);
    return std::max<uint16_t>(static_cast<uint16_t>(limit + 1u), kReleaseCyclesMin);
}

void AtmelMxt224eTouchscreen::Detect(Touch& t, uint8_t slot, const Position& p, AtmelMxt224e& chip) {
    t.state            = TouchState::kDetected;
    t.count            = ReleaseCycles(chip);
    t.moved            = 0u;
    t.raw_x            = p.x;
    t.raw_y            = p.y;
    t.report_x         = p.x;
    t.report_y         = p.y;
    const uint8_t ctrl = chip.Config(kMxtT9Ctrl);
    if ((ctrl & kMxtT9Rpten) != 0u && (ctrl & kDisPrss) == 0u) Report(chip, slot, kMxtT9Detect | kPress, p);
}

void AtmelMxt224eTouchscreen::Follow(Touch& t, uint8_t slot, const Position& p, AtmelMxt224e& chip) {
    t.count         = std::min<uint16_t>(static_cast<uint16_t>(t.count + 1u), ReleaseCycles(chip));
    t.raw_x         = p.x;
    t.raw_y         = p.y;
    const int  hyst = chip.Config(t.moved != 0u ? kT9MovHystN : kT9MovHystI);
    if (std::abs(p.x - t.report_x) <= hyst && std::abs(p.y - t.report_y) <= hyst) return;
    t.moved            = 1u;
    t.report_x         = p.x;
    t.report_y         = p.y;
    const uint8_t ctrl = chip.Config(kMxtT9Ctrl);
    if ((ctrl & kMxtT9Rpten) != 0u && (ctrl & kDisMove) == 0u) Report(chip, slot, kMxtT9Detect | kMove, p);
}

void AtmelMxt224eTouchscreen::Release(Touch& t, uint8_t slot, AtmelMxt224e& chip) {
    const bool     raw  = (chip.Config(kT9YEdgeCtrl) & kRelUpdate) != 0u;
    const Position at   = raw ? Position{t.raw_x, t.raw_y} : Position{t.report_x, t.report_y};
    const uint8_t  ctrl = chip.Config(kMxtT9Ctrl);
    t                   = Touch{};
    if ((ctrl & kMxtT9Rpten) != 0u && (ctrl & kDisRel) == 0u) Report(chip, slot, kRelease, at);
    if (Busy()) return;
    const uint8_t timeout = chip.Config(kMxtT7ActvAcqIntToIdle);
    int64_t       hold    = timeout * kActiveToIdleStep;
    if (timeout == 0u) {
        const uint8_t active = chip.Config(kMxtT7ActvAcqInt);
        hold = active == kFreeRun ? (Down() ? kFreeRunOneTouchNs : kFreeRunNoTouchNs) : active * kNsPerMs;
    }
    active_until_ns_ = emu_.Get<GuestCycleClock>().NowNs() + hold;
}

void AtmelMxt224eTouchscreen::Report(AtmelMxt224e& chip, uint8_t slot, uint8_t status, Position at) const {
    const uint16_t x_range = chip.ConfigRange(kMxtT9XRange);
    const uint16_t y_range = chip.ConfigRange(kMxtT9YRange);
    const uint8_t  orient  = chip.Config(kT9Orient);
    bool           x_12bit = MxtRange12Bit(x_range);
    bool           y_12bit = MxtRange12Bit(y_range);
    if ((orient & kInvertX) != 0u) at.x = static_cast<uint16_t>(x_range - at.x);
    if ((orient & kInvertY) != 0u) at.y = static_cast<uint16_t>(y_range - at.y);
    if ((orient & kSwitch) != 0u) {
        std::swap(at.x, at.y);
        std::swap(x_12bit, y_12bit);
    }
    const uint8_t x_msb = static_cast<uint8_t>(x_12bit ? at.x >> 4u : at.x >> 2u);
    const uint8_t y_msb = static_cast<uint8_t>(y_12bit ? at.y >> 4u : at.y >> 2u);
    const uint8_t x_lsb = static_cast<uint8_t>(x_12bit ? (at.x & 0xFu) << 4u : (at.x & 0x3u) << 6u);
    const uint8_t y_lsb = static_cast<uint8_t>(y_12bit ? at.y & 0xFu : (at.y & 0x3u) << 2u);
    chip.PostTouch(slot, {status, x_msb, y_msb, static_cast<uint8_t>(x_lsb | y_lsb), kTouchArea, 0u, 0u});
}

void AtmelMxt224eTouchscreen::Acquire() {
    armed_     = false;
    auto& chip = emu_.Get<AtmelMxt224e>();
    if (!chip.TouchscreenScanning()) {
        touches_.fill(Touch{});
        tracked_ = -1;
        return;
    }
    if (IntervalNs(chip) == 0) return;
    if (tap_active_ && emu_.Get<GuestCycleClock>().NowNs() >= tap_until_ns_) tap_active_ = false;
    Position p;
    if (Down()) p = Locate(chip);
    if (tracked_ >= 0) {
        Touch& t = touches_[tracked_];
        if (!Down() || Jumped(t, p, chip)) {
            if (t.state == TouchState::kDetecting) t = Touch{};
            tracked_ = -1;
        }
    }
    if (Down() && tracked_ < 0) {
        const int continued = Continued(p, chip);
        const int slot      = continued >= 0 ? continued : FreeSlot(chip);
        if (slot >= 0) tracked_ = static_cast<int8_t>(slot);
        if (continued < 0 && slot >= 0) {
            Touch& t = touches_[slot];
            t        = Touch{};
            t.state  = TouchState::kDetecting;
        }
    }
    for (uint8_t slot = 0u; slot < touches_.size(); ++slot) {
        Touch&     t       = touches_[slot];
        const bool touched = slot == tracked_;
        if (t.state == TouchState::kDetecting) {
            t.raw_x = p.x;
            t.raw_y = p.y;
            if (++t.count >= DetectCycles(chip, slot)) Detect(t, slot, p, chip);
        } else if (t.state == TouchState::kDetected) {
            if (touched) {
                Follow(t, slot, p, chip);
            } else if (--t.count == 0u) {
                Release(t, slot, chip);
            }
        }
    }
    Schedule();
}

void AtmelMxt224eTouchscreen::SaveState(StateWriter& w) {
    static_assert(StateVisitCoversAllBytes<Touch>([](Touch& t, StateFieldBytes& f) { Touch::Visit(t, f); }),
                  "Touch::Visit must name every field of a touch");
    const int64_t now = emu_.Get<GuestCycleClock>().NowNs();
    w.Write<uint8_t>("mxt_touch_armed", armed_ ? 1u : 0u);
    w.Write<int64_t>("mxt_touch_left_ns", due_ns_ - now);
    w.Write<int64_t>("mxt_touch_active_left_ns", std::max<int64_t>(active_until_ns_ - now, 0));
    w.Write<int8_t>("mxt_touch_tracked", tracked_);
    StateWriteField field(w);
    for (Touch& t : touches_) Touch::Visit(t, field);
}

void AtmelMxt224eTouchscreen::RestoreState(StateReader& r) {
    uint8_t       armed     = 0u;
    int64_t       left_ns   = 0;
    int64_t       active_ns = 0;
    const int64_t now       = emu_.Get<GuestCycleClock>().NowNs();
    r.Read("mxt_touch_armed", armed);
    r.Read("mxt_touch_left_ns", left_ns);
    r.Read("mxt_touch_active_left_ns", active_ns);
    r.Read("mxt_touch_tracked", tracked_);
    StateReadField field(r);
    for (Touch& t : touches_) Touch::Visit(t, field);
    armed_           = armed != 0u;
    due_ns_          = now + left_ns;
    active_until_ns_ = now + active_ns;
    host_down_       = false;
    tap_active_      = false;
}

void AtmelMxt224eTouchscreen::PostRestore() {
    if (armed_) Arm(due_ns_);
}

REGISTER_SERVICE(AtmelMxt224eTouchscreen);

}
