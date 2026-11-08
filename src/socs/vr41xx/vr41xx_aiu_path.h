#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../oscillator_ticks.h"
#include "vr41xx_dmaau.h"

#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class EmulatedMemory;
class StateReader;
class StateWriter;
class Vr41xxDcu;

class Vr41xxAiuPathHost {
public:
    virtual void RaiseAiuInt(uint16_t bits)                 = 0;
    virtual bool AiuIntPending(uint16_t bits) const         = 0;
    virtual bool StopAfterOnePage(Vr41xxAiuChannel channel) const = 0;
    virtual bool ConverterEnabled(Vr41xxAiuChannel channel) const = 0;

protected:
    ~Vr41xxAiuPathHost() = default;
};

struct Vr41xxAiuPathConfig {
    Vr41xxAiuChannel channel;
    uint16_t         dma_msk_bit;
    uint16_t         one_page_int;
    uint16_t         two_page_int;
    const char*      name;
};

class Vr41xxAiuPath {
public:
    Vr41xxAiuPath(CerfEmulator& emu, Vr41xxAiuPathHost& host, const Vr41xxAiuPathConfig& config);
    virtual ~Vr41xxAiuPath() = default;

    void Attach();
    void RescaleClock();
    void SetClockRunning(bool running);

    void     SetRateCode(uint16_t code);
    uint16_t RateCode() const { return rate_code_; }
    void     SetEnabled(bool enabled);
    bool     Enabled() const { return enabled_; }

    void CatchUp();
    void Settle();
    void ReleaseStops(uint16_t cleared_bits);
    bool RequestPending() const;
    void Reset();

    void Save(StateWriter& w);
    void Restore(StateReader& r);
    void PostRestore();

protected:
    bool     DmaAllowed() const;
    uint32_t TakeDmaAddress();
    uint64_t HalfwordsToBoundary() const;

    virtual void     Convert()                            = 0;
    virtual void     Serve()                              = 0;
    virtual bool     WantsDma() const                     = 0;
    virtual uint64_t ConversionsToNextChange() const      = 0;
    virtual void     ClearPipeline()                      = 0;
    virtual void     OnStart()                            = 0;
    virtual void     OnStop()                             = 0;
    virtual void     OnRateChanged(uint32_t hz)           = 0;
    virtual void     FinishCatchUp()                      = 0;
    virtual void     SavePipeline(StateWriter& w)         = 0;
    virtual void     RestorePipeline(StateReader& r)      = 0;
    virtual void     ResumeHost()                         = 0;

    uint32_t RateHz() const;

    CerfEmulator&             emu_;
    Vr41xxAiuPathHost&        host_;
    const Vr41xxAiuPathConfig config_;
    EmulatedMemory*           mem_  = nullptr;

private:
    void     Rearm();
    void     PageBoundary();
    uint32_t Page1End(uint32_t base) const;

    StoppableOscillatorTicks ticks_;
    GuestCycleClock*         clock_      = nullptr;
    GuestCycleClock::Event*  event_      = nullptr;
    Vr41xxDcu*               dcu_        = nullptr;
    Vr41xxDmaau*             dmaau_      = nullptr;
    uint16_t                 rate_code_  = 0u;
    bool                     enabled_    = false;
    bool                     clock_running_ = true;
    bool                     dma_started_ = false;
    bool                     stop_one_page_ = false;
    bool                     stop_full_  = false;
    uint64_t                 start_tick_ = 0u;
    uint64_t                 done_       = 0u;
};

}
