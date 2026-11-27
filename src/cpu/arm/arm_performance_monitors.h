#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <cerfcore/core/service.h>
#include <cerfengine/arm/arm_system_host.h>

#include "../../execution/guest_cycle_clock.h"

namespace cerfcore {

class StateReader;
class StateWriter;

class ArmPerformanceMonitors : public Service {
public:
    static constexpr size_t kMaxEventCounters = 31;

    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    cerfengine::ArmCoprocDecision Decide(const cerfengine::ArmCoprocAccess& a);

    uint32_t SelectedCounter() const;

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    static uint32_t CERFENGINE_FASTCALL ReadControl(void* self);
    static void CERFENGINE_FASTCALL WriteControl(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteCountEnableSet(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteCountEnableClear(void* self, uint32_t value);
    static uint32_t CERFENGINE_FASTCALL ReadOverflow(void* self);
    static void CERFENGINE_FASTCALL WriteOverflow(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteSoftwareIncrement(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteSelection(void* self, uint32_t value);
    static uint32_t CERFENGINE_FASTCALL ReadCycleCount(void* self);
    static void CERFENGINE_FASTCALL WriteCycleCount(void* self, uint32_t value);
    static uint32_t CERFENGINE_FASTCALL ReadEventType(void* self);
    static void CERFENGINE_FASTCALL WriteEventType(void* self, uint32_t value);
    static uint32_t CERFENGINE_FASTCALL ReadEventCount(void* self);
    static void CERFENGINE_FASTCALL WriteEventCount(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteUserEnable(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteInterruptEnableSet(void* self, uint32_t value);
    static void CERFENGINE_FASTCALL WriteInterruptEnableClear(void* self, uint32_t value);

    cerfengine::ArmCoprocDecision ControlGroup(const cerfengine::ArmCoprocAccess& a);
    cerfengine::ArmCoprocDecision CounterGroup(const cerfengine::ArmCoprocAccess& a);
    cerfengine::ArmCoprocDecision InterruptGroup(const cerfengine::ArmCoprocAccess& a);

    bool     Counting(uint32_t counter) const;
    bool     CountsCycles(uint32_t counter) const;
    uint64_t CycleCounterSource(uint64_t cycles) const;
    uint32_t ImplementedCounters() const;
    void     Accumulate(uint32_t& count, uint64_t elapsed, uint32_t counter);
    void     Sync();
    void     Rebase();
    void     Schedule();
    void     DriveIrq(bool force);
    void     Reset();

    GuestCycleClock*        clock_          = nullptr;
    GuestCycleClock::Event* overflow_event_ = nullptr;

    uint32_t identification_   = 0;
    uint32_t event_counters_   = 0;
    uint32_t interrupt_line_   = 0;
    uint32_t control_          = 0;
    uint32_t count_enable_     = 0;
    uint32_t interrupt_enable_ = 0;
    uint32_t overflow_         = 0;
    uint32_t selection_        = 0;
    uint32_t user_enable_      = 0;
    uint32_t cycle_count_      = 0;
    uint64_t cycle_base_       = 0;
    bool     irq_level_        = false;
    std::array<uint32_t, kMaxEventCounters> event_type_{};
    std::array<uint32_t, kMaxEventCounters> event_count_{};
    std::array<uint64_t, kMaxEventCounters> event_base_{};
};

}
