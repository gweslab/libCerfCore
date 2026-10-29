#pragma once

#include "instance_halt.h"
#include "log.h"
#include "service.h"

#include <cerfengine/host_exception.h>

#include <atomic>
#include <exception>
#include <functional>
#include <vector>

namespace cerfengine { class GuestEngine; }

namespace cerfcore {

class JitRunner;

class Fatal : public Service {
public:
    using CatchScope = cerfengine::PlainRaiseScope;

    explicit Fatal(CerfEmulator& emu);

    void OnReady() override;

    [[noreturn]] void        Die(const char* fmt, ...);
    [[noreturn]] void        UserError(const char* fmt, ...);
    [[noreturn]] static void LocatorFailure(const char* reason);
    [[noreturn]] static void Raise(std::exception_ptr failure);

    void RegisterHaltListener(std::function<void(int code)> fn) { halt_listeners_.push_back(std::move(fn)); }
    bool Halted() const { return halt_code_.load(std::memory_order_acquire) != kNotHalted; }
    int  HaltCode() const { return halt_code_.load(std::memory_order_acquire); }
    std::string CrashLogPath() const;

    template <class Fn>
    bool Guard(Fn&& fn);

    template <class Fn>
    void GuardTeardown(Fn&& fn);

    template <class Fn>
    std::exception_ptr RunForWaiter(Fn&& fn);

    void SetLiveEngine(cerfengine::GuestEngine* engine) {
        live_engine_.store(engine, std::memory_order_release);
    }

private:
    static constexpr int kNotHalted = -1;

    [[noreturn]] static void Halt(int code);

    void OnHalt(const InstanceHalt& halt);

    std::atomic<cerfengine::GuestEngine*>  live_engine_{nullptr};
    std::atomic<int>                       halt_code_{kNotHalted};
    Log::CautionRing                       cautions_;
    JitRunner*                             jit_ = nullptr;
    std::vector<std::function<void(int)>>  halt_listeners_;
};

template <class Fn>
bool Fatal::Guard(Fn&& fn) {
    if (Halted()) return false;
    try {
        const Log::CautionScope scope(cautions_);
        fn();
        return true;
    } catch (const InstanceHalt& halt) {
        OnHalt(halt);
    }
    return false;
}

template <class Fn>
void Fatal::GuardTeardown(Fn&& fn) {
    try {
        const Log::CautionScope scope(cautions_);
        fn();
    } catch (const InstanceHalt& halt) {
        OnHalt(halt);
    }
}

template <class Fn>
std::exception_ptr Fatal::RunForWaiter(Fn&& fn) {
    try {
        const Log::CautionScope scope(cautions_);
        fn();
    } catch (const InstanceHalt&) {
        return std::current_exception();
    }
    return nullptr;
}

}
