#include <cerfcore/core/log.h>
#include "../platform/host_crash.h"
#include <atomic>
#include <cstdarg>
#include <cstdint>

namespace cerfcore {

static std::atomic<bool> g_emergency_started{false};

void Log::EmergencyDumpAllThreadStacks() {
    static std::atomic<bool> dumped{false};
    bool expected = false;
    if (!dumped.compare_exchange_strong(expected, true)) return;
    host_platform::CrashDumpOtherThreads();
}

bool Log::IsEmergencyActive() {
    return g_emergency_started.load(std::memory_order_acquire);
}

void Log::EmergencyStart() {
    bool expected = false;
    if (!g_emergency_started.compare_exchange_strong(expected, true))
        return;
    host_platform::CrashFreezeOtherThreads();
    host_platform::CrashOpenLog();
}

void Log::Emergency(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = host_platform::CrashFormat(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n <= 0) return;
    if (n > (int)sizeof(buf)) n = sizeof(buf);
    host_platform::CrashWrite(buf, (size_t)n);
}

void Log::EmergencyWriteBytes(const char* buf, size_t len) {
    host_platform::CrashWrite(buf, len);
}

bool Log::SymbolizeAddress(const void* addr, char* out, size_t out_size) {
    return host_platform::CrashSymbolize(addr, out, out_size);
}

void Log::InstallCrashHandler() {
    host_platform::CrashInstallUnhandledFilter();
}

}
