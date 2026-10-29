#include <cerfcore/core/fatal.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/cerf_paths.h>
#include <cerfcore/core/log.h>
#include <cerfcore/execution/jit_runner.h>
#include <cerfcore/platform/host_file.h>
#include <cerfengine/guest_engine.h>
#include <cerfengine/host_exception.h>

#include <cstdarg>
#include <cstdio>
#include <utility>

namespace cerfcore {

REGISTER_SERVICE(Fatal);

Fatal::Fatal(CerfEmulator& emu) : Service(emu) {}

void Fatal::OnReady() { jit_ = &emu_.Get<JitRunner>(); }

std::string Fatal::CrashLogPath() const {
    const char* configured = emu_.Config().crash_log_file;
    return configured ? std::string(configured) : GetCerfDir() + kCrashLogFileName;
}

void Fatal::Die(const char* fmt, ...) {
    char reason[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);

    if (cerfengine::GuestEngine* engine = live_engine_.load(std::memory_order_acquire))
        engine->PrintFatalDump();
    else
        LOG(Caution, "no guest context: the engine is not live\n");

    LOG(Caution, "%s\n", reason);

    Halt(CERF_FATAL_RUNTIME_ERROR);
}

void Fatal::UserError(const char* fmt, ...) {
    char reason[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(reason, sizeof(reason), fmt, ap);
    va_end(ap);

    LOG(Caution, "%s\n", reason);

    Halt(CERF_FATAL_USER_ERROR);
}

void Fatal::LocatorFailure(const char* reason) {
    LOG(Caution, "%s\n", reason);
    Halt(CERF_FATAL_RUNTIME_ERROR);
}

void Fatal::Halt(int code) {
    LOG(Caution, "CERF is halting (fatal code = %d)\n", code);
    Raise(std::make_exception_ptr(InstanceHalt(code)));
}

void Fatal::Raise(std::exception_ptr failure) { cerfengine::RaiseHostFailure(std::move(failure)); }

void Fatal::OnHalt(const InstanceHalt& halt) {
    int running = kNotHalted;
    if (!halt_code_.compare_exchange_strong(running, halt.Code(), std::memory_order_acq_rel)) return;
    if (halt.Code() != CERF_FATAL_USER_ERROR) {
        const std::string path  = CrashLogPath();
        uint32_t          error = 0;
        if (host_platform::ReplaceHostFileContents(path, halt.Report(cautions_), true, error)) {
            LOG(Cerf, "this instance ends here; the crash tail is in %s\n", path.c_str());
        } else {
            LOG(Caution, "this instance ends here; %s cannot be written (error %u)\n", path.c_str(), error);
        }
    }
    jit_->RequestStop();
    for (auto& fn : halt_listeners_) fn(halt.Code());
}

}
