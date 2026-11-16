#include "imx6_gic.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/engine_bridge.h"
#include <cerfengine/arm/arm_jit.h>
#include "../../cpu/arm/arm_cp15_registers.h"
#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include "imx6_a9_timers.h"
#include "imx6_gic_aux.h"
#include "imx6_gic_identity.h"
#include "imx6_gpc.h"

#include <algorithm>
#include <mutex>
#include "imx6_id.h"

namespace cerfcore {

namespace {

using imx6_gic_detail::Imx6A9Timers;
using imx6_gic_detail::Imx6GicAux;

class Imx6GicImpl final : public cerfcore::Imx6Gic {
public:
    using cerfcore::Imx6Gic::Imx6Gic;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }
    void AssertSpi(int spi) override {
        const int gic_id = spi + 32;
        const int w = gic_id >> 5;
        const uint32_t bit = 1u << (gic_id & 31);
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            if (w < 5) {
                const bool was_high = (line_level_[w] & bit) != 0u;
                line_level_[w] |= bit;
                level_pending_dirty_ = true;
                if (!IsEdgeTriggeredLocked(gic_id) || !was_high) pending_[w] |= bit;
            }
            deliver = DeliverPendingLocked();
        }
        PublishLine(deliver);
    }
    void DeAssertSpi(int spi) override {
        const int gic_id = spi + 32;
        const int w = gic_id >> 5;
        const uint32_t bit = 1u << (gic_id & 31);
        UpdateAndDeliver([&] {
            if (w < 5) {
                line_level_[w] &= ~bit;
                level_pending_dirty_ = true;
                if (!IsEdgeTriggeredLocked(gic_id)) pending_[w] &= ~bit;
            }
        });
    }

    uint32_t ReadMmio(uint32_t off) override {
        switch (off) {
        case 0x100: { std::lock_guard<std::mutex> lk(timer_mutex_); return cpu_control_; }
        case 0x104: { std::lock_guard<std::mutex> lk(timer_mutex_); return priority_mask_; }
        case 0x108: { std::lock_guard<std::mutex> lk(timer_mutex_); return binary_point_; }
        case 0x114: { std::lock_guard<std::mutex> lk(timer_mutex_); return running_priority_; }

        case 0x10C: {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            CatchUpTimersLocked();
            RefreshLevelPendingLocked();
            const uint32_t id = HighestPriorityPendingLocked();
            if (id == 1023u) {
                emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().ClearInterruptPending();
                return 1023u;
            }
            active_stack_irq_[active_depth_] = id;
            active_stack_priority_[active_depth_] = PriorityOfLocked(id);
            ++active_depth_;
            active_irq_ = id;
            running_priority_ = active_stack_priority_[active_depth_ - 1u];
            pending_[id >> 5] &= ~(1u << (id & 31));
            level_pending_dirty_ = true;
            if (DeliverPendingLocked())
                emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().SetInterruptPending();
            else
                emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().ClearInterruptPending();
            return id;
        }
        case 0x1FC: return imx6_gic_detail::kIccIdr;
        case 0x1000: { std::lock_guard<std::mutex> lk(timer_mutex_); return distributor_control_; }
        case 0x1004: return imx6_gic_detail::kIcdIctr;
        case 0x1008: return imx6_gic_detail::kIcdIidr;
        default: break;
        }

        if (off >= 0x1100u && off < 0x1114u) { std::lock_guard<std::mutex> lk(timer_mutex_); return enabled_[(off - 0x1100u) >> 2]; }
        if (off >= 0x1180u && off < 0x1194u) { std::lock_guard<std::mutex> lk(timer_mutex_); return enabled_[(off - 0x1180u) >> 2]; }
        if (off >= 0x1200u && off < 0x1214u) { std::lock_guard<std::mutex> lk(timer_mutex_); return pending_[(off - 0x1200u) >> 2]; }
        if (off >= 0x1280u && off < 0x1294u) { std::lock_guard<std::mutex> lk(timer_mutex_); return pending_[(off - 0x1280u) >> 2]; }
        if (off >= 0x1400u && off < 0x14A0u) { std::lock_guard<std::mutex> lk(timer_mutex_); return priorities_[(off - 0x1400u) >> 2]; }
        if (imx6_gic_detail::kIcdiptrReadAsZeroWriteIgnored && off >= 0x1800u && off < 0x18A0u) return 0u;
        if (off >= 0x1C00u && off < 0x1C28u) { std::lock_guard<std::mutex> lk(timer_mutex_); return configuration_[(off - 0x1C00u) >> 2]; }
        {
            uint32_t value = 0u;
            if (aux_.ScuRead(off, emu_.Get<ArmCp15Registers>().File().aux_control, value)) return value;
            bool handled = false;
            bool deliver = false;
            {
                std::lock_guard<std::mutex> lk(timer_mutex_);
                CatchUpTimersLocked();
                handled = timers_.Read(off, value);
                if (handled) {
                    RefreshLevelPendingLocked();
                    deliver = DeliverPendingLocked();
                }
            }
            if (handled) {
                PublishLine(deliver);
                return value;
            }
        }
        emu_.Get<Fatal>().Die("[GIC] unsupported read32 at 0x%08X", kMmioBase + off);
    }

    void WriteMmio(uint32_t off, uint32_t value) override {
        switch (off) {
        case 0x100:
            if (((cpu_control_ ^ value) & (1u << 3)) != 0u)
                emu_.Get<Fatal>().Die("[GIC] ICCICR FIQEn has no FIQ delivery model at 0x%08X (0x%08X)",
                                      kMmioBase + off, value);
            if (((cpu_control_ ^ value) & (1u << 4)) != 0u)
                emu_.Get<Fatal>().Die("[GIC] ICCICR SBPR/CBPR has no split-priority model at 0x%08X (0x%08X)",
                                      kMmioBase + off, value);
            if (((cpu_control_ ^ value) & ~7u) != 0u)
                emu_.Get<Fatal>().Die("[GIC] ICCICR bit with no modeled effect changed at 0x%08X (0x%08X)",
                                      kMmioBase + off, value);
            UpdateAndDeliver([&] { cpu_control_ = value & 7u; });
            return;
        case 0x104: UpdateAndDeliver([&] { priority_mask_ = value & 0xF8u; }); return;
        case 0x108: UpdateAndDeliver([&] { binary_point_ = value & 7u; }); return;
        case 0x110:
            UpdateAndDeliver([&] {
                const uint32_t id = value & 0x3FFu;
                for (uint32_t i = active_depth_; i != 0u; --i) {
                    if (active_stack_irq_[i - 1u] != id) continue;
                    for (uint32_t j = i; j < active_depth_; ++j) {
                        active_stack_irq_[j - 1u] = active_stack_irq_[j];
                        active_stack_priority_[j - 1u] = active_stack_priority_[j];
                    }
                    --active_depth_;
                    active_stack_irq_[active_depth_] = 1023u;
                    active_stack_priority_[active_depth_] = 0xFFu;
                    break;
                }
                active_irq_ = active_depth_ == 0u ? 1023u : active_stack_irq_[active_depth_ - 1u];
                running_priority_ = active_depth_ == 0u ? 0xFFu : active_stack_priority_[active_depth_ - 1u];
            });
            return;
        case 0x1000: UpdateAndDeliver([&] { distributor_control_ = value & 3u; }); return;
        default: break;
        }
        if (off >= 0x1080u && off < 0x1094u) {
            if (value != 0u)
                emu_.Get<Fatal>().Die("[GIC] interrupt grouping is not modelled (0x%08X at 0x%08X)", value, kMmioBase + off);
            return;
        }
        if (off >= 0x1100u && off < 0x1114u) {
            UpdateAndDeliver([&] {
                enabled_[(off - 0x1100u) >> 2] |= value;
                level_pending_dirty_ = true;
            });
            return;
        }
        if (off >= 0x1180u && off < 0x1194u) {
            UpdateAndDeliver([&] {
                enabled_[(off - 0x1180u) >> 2] &= ~value;
                level_pending_dirty_ = true;
            });
            return;
        }
        if (off >= 0x1200u && off < 0x1214u) {
            if (off == 0x1200u && value != 0u)
                emu_.Get<Fatal>().Die("[GIC] set-pending of ids 0-31 is not modelled (0x%08X)", value);
            UpdateAndDeliver([&] {
                pending_[(off - 0x1200u) >> 2] |= value;
                level_pending_dirty_ = true;
            });
            return;
        }
        if (off >= 0x1280u && off < 0x1294u) {
            UpdateAndDeliver([&] {
                pending_[(off - 0x1280u) >> 2] &= ~value;
                level_pending_dirty_ = true;
            });
            return;
        }
        if (off >= 0x1400u && off < 0x14A0u) {
            UpdateAndDeliver([&] { priorities_[(off - 0x1400u) >> 2] = value & 0xF8F8F8F8u; });
            return;
        }
        if (imx6_gic_detail::kIcdiptrReadAsZeroWriteIgnored && off >= 0x1800u && off < 0x18A0u) return;
        if (off >= 0x1C00u && off < 0x1C28u) {
            const uint32_t index = (off - 0x1C00u) >> 2;
            if (index >= 2u)
                UpdateAndDeliver([&] {
                    configuration_[index] = (value & 0xAAAAAAAAu) | 0x55555555u;
                    level_pending_dirty_ = true;
                });
            return;
        }
        {
            const char* unmodelled = nullptr;
            if (aux_.ScuWrite(off, value, unmodelled)) {
                if (unmodelled != nullptr)
                    emu_.Get<Fatal>().Die("[GIC] %s is not modelled (0x%08X)", unmodelled, value);
                return;
            }
        }
        bool handled = false;
        UpdateAndDeliver([&] { handled = timers_.Write(off, value); });
        if (handled) return;
        emu_.Get<Fatal>().Die("[GIC] unsupported write32 at 0x%08X val=0x%08X", kMmioBase + off, value);
    }

    void SaveGicState(StateWriter& w) override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        CatchUpTimersLocked();
        w.Write("active_irq", active_irq_);
        w.Write("cpu_control", cpu_control_);
        w.Write("priority_mask", priority_mask_);
        w.Write("binary_point", binary_point_);
        w.Write("running_priority", running_priority_);
        w.Write("active_depth", active_depth_);
        w.WriteBytes("active_stack_irq", active_stack_irq_, sizeof(active_stack_irq_));
        w.WriteBytes("active_stack_priority", active_stack_priority_, sizeof(active_stack_priority_));
        w.Write("distributor_control", distributor_control_);
        timers_.SaveState(w);
        aux_.SaveState(w);
        w.WriteBytes("enabled", enabled_, sizeof(enabled_));
        w.WriteBytes("pending", pending_, sizeof(pending_));
        w.WriteBytes("line_level", line_level_, sizeof(line_level_));
        w.WriteBytes("priorities", priorities_, sizeof(priorities_));
        w.WriteBytes("configuration", configuration_, sizeof(configuration_));
    }
    void RestoreGicState(StateReader& r) override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        r.Read("active_irq", active_irq_);
        r.Read("cpu_control", cpu_control_);
        r.Read("priority_mask", priority_mask_);
        r.Read("binary_point", binary_point_);
        r.Read("running_priority", running_priority_);
        r.Read("active_depth", active_depth_);
        r.ReadBytes("active_stack_irq", active_stack_irq_, sizeof(active_stack_irq_));
        r.ReadBytes("active_stack_priority", active_stack_priority_, sizeof(active_stack_priority_));
        r.Read("distributor_control", distributor_control_);
        timers_.RestoreState(r);
        aux_.RestoreState(r);
        r.ReadBytes("enabled", enabled_, sizeof(enabled_));
        r.ReadBytes("pending", pending_, sizeof(pending_));
        r.ReadBytes("line_level", line_level_, sizeof(line_level_));
        r.ReadBytes("priorities", priorities_, sizeof(priorities_));
        r.ReadBytes("configuration", configuration_, sizeof(configuration_));
        level_pending_dirty_ = true;
    }
    void PostRestoreGicState() override {
        std::lock_guard<std::mutex> lk(timer_mutex_);
        timers_.Resume();
        RefreshLevelPendingLocked();
        PublishLine(DeliverPendingLocked());
    }

    void SetLowPowerMode(uint32_t mode) override {
        UpdateAndDeliver([this, mode] {
            if (mode == 2u) timers_.SuspendForGatedClock();
            else timers_.ResumeFromGatedClock();
        });
    }

    void OnReady() override {
        auto& clock = emu_.Get<GuestCycleClock>();
        gpc_ = &emu_.Get<Imx6Gpc>();
        timers_.Attach(&clock, clock.Add([this] { OnTimerEvent(); }),
                       clock.Add([this] { OnTimerEvent(); }));
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            {
                std::lock_guard<std::mutex> lk(timer_mutex_);
                ResetLocked();
            }
            emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().ClearInterruptPending();
        });
    }

private:
    void ResetLocked() {
        active_irq_ = 1023u;
        cpu_control_ = 0;
        priority_mask_ = 0;
        binary_point_ = 2u;
        running_priority_ = 0xFFu;
        active_depth_ = 0u;
        std::fill(std::begin(active_stack_irq_), std::end(active_stack_irq_), 1023u);
        std::fill(std::begin(active_stack_priority_), std::end(active_stack_priority_), 0xFFu);
        distributor_control_ = 0;
        std::fill(std::begin(enabled_), std::end(enabled_), 0u);
        std::fill(std::begin(pending_), std::end(pending_), 0u);
        std::fill(std::begin(line_level_), std::end(line_level_), 0u);
        std::fill(std::begin(priorities_), std::end(priorities_), 0u);
        std::fill(std::begin(configuration_), std::end(configuration_), 0u);
        configuration_[0] = 0xAAAAAAAAu;
        configuration_[1] = 0x7DC00000u;
        std::fill(std::begin(configuration_) + 2, std::end(configuration_), 0x55555555u);
        level_pending_dirty_ = true;
        timers_.Reset();
        aux_.Reset();
    }

    void OnTimerEvent() {
        UpdateAndDeliver([] {});
    }

    void CatchUpTimersLocked() {
        const uint32_t pending = timers_.CatchUp();
        pending_[0] |= pending;
        if (pending != 0u) level_pending_dirty_ = true;
    }

    bool IsEdgeTriggeredLocked(int gic_id) const {
        if (gic_id < 32 || gic_id >= 160) return false;
        const uint32_t cfg = configuration_[gic_id >> 4];
        const uint32_t hi_bit = 1u << (((gic_id & 15) * 2u) + 1u);
        return (cfg & hi_bit) != 0u;
    }

    void RefreshLevelPendingLocked() {
        if (!level_pending_dirty_) return;
        level_pending_dirty_ = false;
        for (int gic_id = 32; gic_id < 160; ++gic_id) {
            const int w = gic_id >> 5;
            const uint32_t bit = 1u << (gic_id & 31);
            if ((line_level_[w] & bit) != 0u && !IsEdgeTriggeredLocked(gic_id)) pending_[w] |= bit;
        }
    }

    bool PendingEnabledLocked(uint32_t id) const {
        if (id < 32u && id != 27u && id != 29u) return false;
        if (!gpc_->AllowsWake(id)) return false;
        for (uint32_t i = 0u; i < active_depth_; ++i)
            if (active_stack_irq_[i] == id) return false;
        if ((distributor_control_ & cpu_control_ & 1u) == 0) return false;
        return (pending_[id >> 5] & enabled_[id >> 5] & (1u << (id & 31))) != 0u;
    }

    uint32_t PriorityOfLocked(uint32_t id) const {
        return (priorities_[id >> 2] >> ((id & 3u) * 8u)) & 0xFFu;
    }

    uint32_t HighestPriorityPendingLocked() const {
        uint32_t best = 1023u;
        uint32_t best_priority = 0x100u;
        if ((distributor_control_ & cpu_control_ & 1u) != 0u) {
            for (uint32_t word = 0u; word < 5u; ++word) {
                uint32_t candidates = pending_[word] & enabled_[word];
                while (candidates != 0u) {
                    uint32_t bit_index = 0u;
                    uint32_t bits = candidates;
                    while ((bits & 1u) == 0u) {
                        bits >>= 1u;
                        ++bit_index;
                    }
                    const uint32_t id = word * 32u + bit_index;
                    candidates &= candidates - 1u;
                    if (!PendingEnabledLocked(id)) continue;
                    const uint32_t priority = PriorityOfLocked(id);
                    if (priority < best_priority) {
                        best_priority = priority;
                        best = id;
                    }
                }
            }
        }
        if (best_priority >= priority_mask_ ||
            (active_depth_ != 0u &&
            (best_priority >> (binary_point_ + 1u)) >=
                (running_priority_ >> (binary_point_ + 1u)))) best = 1023u;
        return best;
    }

    bool DeliverPendingLocked() const {
        return HighestPriorityPendingLocked() != 1023u;
    }

    std::mutex timer_mutex_;
    bool level_pending_dirty_ = true;

    template <typename Update> void UpdateAndDeliver(Update&& update) {
        bool deliver = false;
        {
            std::lock_guard<std::mutex> lk(timer_mutex_);
            CatchUpTimersLocked();
            update();
            RefreshLevelPendingLocked();
            deliver = DeliverPendingLocked();
        }
        PublishLine(deliver);
    }
    void PublishLine(bool deliver) {
        if (deliver) emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().SetInterruptPending();
        else         emu_.Get<EngineBridge>().Engine().Get<cerfengine::ArmJit>().ClearInterruptPending();
    }
    Imx6A9Timers timers_;
    Imx6GicAux aux_;
    Imx6Gpc* gpc_ = nullptr;
    uint32_t active_irq_ = 1023u;
    uint32_t cpu_control_ = 0;
    uint32_t priority_mask_ = 0;
    uint32_t binary_point_ = 2u;
    uint32_t running_priority_ = 0xFFu;
    uint32_t active_depth_ = 0u;
    uint32_t active_stack_irq_[160]{};
    uint32_t active_stack_priority_[160]{};
    uint32_t distributor_control_ = 0;
    uint32_t enabled_[5]{};
    uint32_t pending_[5]{};
    uint32_t line_level_[5]{};
    uint32_t priorities_[40]{};
    uint32_t configuration_[10]{};
};

}

REGISTER_SERVICE_AS(Imx6GicImpl, cerfcore::Imx6Gic);

}
