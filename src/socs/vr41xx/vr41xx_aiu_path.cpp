#include "vr41xx_aiu_path.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>
#include "vr41xx_dcu.h"

#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kPageBytes  = 0x400u;
constexpr uint32_t kSpaceBytes = 0x800u;

}

Vr41xxAiuPath::Vr41xxAiuPath(CerfEmulator& emu, Vr41xxAiuPathHost& host, const Vr41xxAiuPathConfig& config)
    : emu_(emu), host_(host), config_(config), ticks_(emu, false) {}

void Vr41xxAiuPath::Attach() {
    mem_   = &emu_.Get<EmulatedMemory>();
    dcu_   = &emu_.Get<Vr41xxDcu>();
    dmaau_ = &emu_.Get<Vr41xxDmaau>();
    clock_ = &emu_.Get<GuestCycleClock>();
    ticks_.Attach(RateHz(), 1u);
    event_ = clock_->Add([this] {
        CatchUp();
        Rearm();
    });
}

void Vr41xxAiuPath::RescaleClock() {
    ticks_.Rescale();
    Rearm();
}

void Vr41xxAiuPath::SetClockRunning(bool running) {
    if (running == clock_running_) return;
    CatchUp();
    ticks_.SetCounting(running);
    clock_running_ = running;
    Rearm();
}

uint32_t Vr41xxAiuPath::RateHz() const {
    switch (rate_code_) {
        case 0u: return 11025u;
        case 1u: return 22050u;
        case 2u: return 44100u;
        case 4u: return 8000u;
        default:
            emu_.Get<Fatal>().Die("Vr41xxAiu: %s conversion rate code %u is RFU", config_.name, rate_code_);
    }
}

void Vr41xxAiuPath::SetRateCode(uint16_t code) {
    if (!enabled_) {
        rate_code_ = code;
        return;
    }
    CatchUp();
    rate_code_ = code;
    ticks_.SetOscRate(RateHz(), 1u);
    OnRateChanged(RateHz());
    Rearm();
}

void Vr41xxAiuPath::SetEnabled(bool enabled) {
    if (enabled == enabled_) return;
    if (!enabled) {
        CatchUp();
        OnStop();
        enabled_ = false;
        ClearPipeline();
        dma_started_   = false;
        stop_one_page_ = false;
        stop_full_     = false;
        clock_->Disarm(event_);
        return;
    }
    ticks_.SetOscRate(RateHz(), 1u);
    enabled_       = true;
    dma_started_   = false;
    stop_one_page_ = false;
    stop_full_     = false;
    start_tick_  = ticks_.Now();
    done_        = 0u;
    OnStart();
    Settle();
}

void Vr41xxAiuPath::CatchUp() {
    if (!enabled_) return;
    const uint64_t target = ticks_.Now() - start_tick_;
    while (done_ < target) {
        ++done_;
        Convert();
    }
    FinishCatchUp();
}

void Vr41xxAiuPath::Settle() {
    if (!enabled_) return;
    Serve();
    Rearm();
}

void Vr41xxAiuPath::ReleaseStops(uint16_t cleared_bits) {
    if (cleared_bits & config_.one_page_int) stop_one_page_ = false;
    if (cleared_bits & config_.two_page_int) stop_full_ = false;
}

bool Vr41xxAiuPath::RequestPending() const {
    return enabled_ && WantsDma() && !stop_one_page_ && !stop_full_ && !dcu_->DmaEnabled(config_.dma_msk_bit);
}

void Vr41xxAiuPath::Reset() {
    if (enabled_) OnStop();
    enabled_     = false;
    rate_code_     = 0u;
    dma_started_   = false;
    stop_one_page_ = false;
    stop_full_     = false;
    ClearPipeline();
    clock_->Disarm(event_);
}

bool Vr41xxAiuPath::DmaAllowed() const {
    return enabled_ && dcu_->DmaEnabled(config_.dma_msk_bit) && !stop_one_page_ && !stop_full_;
}

uint32_t Vr41xxAiuPath::Page1End(uint32_t base) const {
    if (base & kPageBytes) {
        emu_.Get<Fatal>().Die("Vr41xxAiu: %s DMA base 0x%08X lies in the second page of its 2-Kbyte space",
                              config_.name, base);
    }
    return (base & ~(kPageBytes - 1u)) + kPageBytes;
}

uint32_t Vr41xxAiuPath::TakeDmaAddress() {
    const uint32_t base = dmaau_->Base(config_.channel);
    if (!dma_started_) {
        dmaau_->SetAddress(config_.channel, base);
        dma_started_ = true;
    }
    const uint32_t page1_end = Page1End(base);
    const uint32_t page2_end = page1_end + kPageBytes;
    const uint32_t addr      = dmaau_->Address(config_.channel);
    if (addr < page2_end - kSpaceBytes || addr >= page2_end) {
        emu_.Get<Fatal>().Die("Vr41xxAiu: %s DMA address 0x%08X is outside the 2-Kbyte space of base 0x%08X",
                              config_.name, addr, base);
    }
    uint32_t next = addr + 2u;
    if (next == page1_end) {
        PageBoundary();
        next = base + kPageBytes;
    } else if (next == page2_end) {
        PageBoundary();
        next = base;
    }
    dmaau_->SetAddress(config_.channel, next);
    return addr;
}

void Vr41xxAiuPath::PageBoundary() {
    const bool unserviced = host_.AiuIntPending(config_.one_page_int);
    host_.RaiseAiuInt(config_.one_page_int);
    if (unserviced) {
        host_.RaiseAiuInt(config_.two_page_int);
        stop_full_ = true;
    }
    if (host_.StopAfterOnePage(config_.channel)) stop_one_page_ = true;
}

uint64_t Vr41xxAiuPath::HalfwordsToBoundary() const {
    const uint32_t base      = dmaau_->Base(config_.channel);
    const uint32_t page1_end = Page1End(base);
    const uint32_t addr      = dma_started_ ? dmaau_->Address(config_.channel) : base;
    if (addr < page1_end) return (page1_end - addr) / 2u;
    const uint32_t page2_end = page1_end + kPageBytes;
    return addr < page2_end ? (page2_end - addr) / 2u : 1u;
}

void Vr41xxAiuPath::Rearm() {
    if (!enabled_) {
        clock_->Disarm(event_);
        return;
    }
    const uint64_t n = ConversionsToNextChange();
    if (n == 0u) {
        clock_->Disarm(event_);
        return;
    }
    ticks_.ArmAt(event_, start_tick_ + done_ + n);
}

void Vr41xxAiuPath::Save(StateWriter& w) {
    const std::string p = std::string(config_.name) + "_";
    w.Write((p + "rate").c_str(), rate_code_);
    w.Write<uint8_t>((p + "enabled").c_str(), enabled_ ? 1u : 0u);
    w.Write<uint8_t>((p + "clock_running").c_str(), clock_running_ ? 1u : 0u);
    w.Write<uint8_t>((p + "dma_started").c_str(), dma_started_ ? 1u : 0u);
    w.Write<uint8_t>((p + "stop_one_page").c_str(), stop_one_page_ ? 1u : 0u);
    w.Write<uint8_t>((p + "stop_full").c_str(), stop_full_ ? 1u : 0u);
    w.Write((p + "start_tick").c_str(), start_tick_);
    w.Write((p + "done").c_str(), done_);
    ticks_.Save(w, p.c_str());
    SavePipeline(w);
}

void Vr41xxAiuPath::Restore(StateReader& r) {
    const std::string p = std::string(config_.name) + "_";
    uint8_t enabled = 0u, running = 0u, started = 0u, stop_one = 0u, stop_full = 0u;
    r.Read((p + "rate").c_str(), rate_code_);
    r.Read((p + "enabled").c_str(), enabled);
    r.Read((p + "clock_running").c_str(), running);
    r.Read((p + "dma_started").c_str(), started);
    r.Read((p + "stop_one_page").c_str(), stop_one);
    r.Read((p + "stop_full").c_str(), stop_full);
    r.Read((p + "start_tick").c_str(), start_tick_);
    r.Read((p + "done").c_str(), done_);
    ticks_.Restore(r, p.c_str());
    RestorePipeline(r);
    enabled_       = enabled != 0u;
    clock_running_ = running != 0u;
    dma_started_   = started != 0u;
    stop_one_page_ = stop_one != 0u;
    stop_full_     = stop_full != 0u;
    clock_->Disarm(event_);
}

void Vr41xxAiuPath::PostRestore() {
    ResumeHost();
    Rearm();
}

}
