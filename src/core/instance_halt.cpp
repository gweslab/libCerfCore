#include <cerfcore/core/instance_halt.h>

#include <cerfcore/core/log.h>
#include "../platform/host_backtrace.h"
#include "../platform/host_crash.h"
#include "../platform/host_thread.h"

#include <cstdio>

namespace cerfcore {

namespace {

constexpr size_t kCautionsBytes = 4096;
constexpr size_t kLineBytes     = 640;
constexpr size_t kSymbolBytes   = 512;

}

InstanceHalt::InstanceHalt(int code)
    : code_(code),
      thread_id_(host_platform::CurrentThreadId()),
      frame_count_(host_platform::CaptureBacktrace(frames_, kMaxFrames)) {}

std::string InstanceHalt::Report(const Log::CautionRing& cautions) const {
    char        line[kLineBytes];
    std::string report;
    std::snprintf(line, sizeof(line), "=== CERF instance halted (code %d) on thread %lu ===\n", code_,
                  static_cast<unsigned long>(thread_id_));
    report += line;
    char recent[kCautionsBytes];
    Log::CopyCautions(cautions, recent, sizeof(recent));
    if (recent[0] != '\0') report += std::string("[FATAL] recent cautions:\n") + recent;
    report += "[FATAL] stack trace:\n";
    char symbol[kSymbolBytes];
    for (int i = 0; i < frame_count_; ++i) {
        if (host_platform::CrashSymbolize(frames_[i], symbol, sizeof(symbol))) {
            std::snprintf(line, sizeof(line), "[FATAL]   [%d] %s\n", i, symbol);
        } else {
            std::snprintf(line, sizeof(line), "[FATAL]   [%d] %p\n", i, frames_[i]);
        }
        report += line;
    }
    return report;
}

}
