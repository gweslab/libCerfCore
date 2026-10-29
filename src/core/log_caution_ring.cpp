#include "log_caution_ring.h"

#include "log_mutex.h"

#include <cerfcore/core/log.h>

#include <cstdio>
#include <cstring>

namespace cerfcore {

namespace {

Log::CautionRing              g_process_cautions;
thread_local Log::CautionRing* t_instance_cautions = nullptr;

void Record(Log::CautionRing& ring, const char* fmt, va_list args) {
    char* slot = ring.lines[ring.next];
    vsnprintf(slot, Log::CautionRing::kLineBytes, fmt, args);
    slot[Log::CautionRing::kLineBytes - 1] = '\0';

    size_t len = strlen(slot);
    while (len > 0 && (slot[len - 1] == '\n' || slot[len - 1] == '\r'))
        slot[--len] = '\0';

    ring.next = (ring.next + 1) % Log::CautionRing::kLines;
    if (ring.count < Log::CautionRing::kLines) ring.count++;
}

void CopyRing(const Log::CautionRing& ring, char* out, size_t out_size) {
    out[0] = '\0';
    size_t written = 0;
    size_t first   = (ring.next + Log::CautionRing::kLines - ring.count) % Log::CautionRing::kLines;
    for (size_t i = 0; i < ring.count && written + 2 < out_size; i++) {
        const char* line = ring.lines[(first + i) % Log::CautionRing::kLines];
        if (line[0] == '\0') continue;
        size_t k = 0;
        while (k < Log::CautionRing::kLineBytes && line[k] != '\0' && written + 2 < out_size) {
            out[written++] = line[k++];
        }
        out[written++] = '\n';
        out[written]   = '\0';
    }
}

}

namespace log_detail {

void RecordCautionLocked(const char* fmt, va_list args) {
    va_list instance_args;
    va_copy(instance_args, args);
    Record(g_process_cautions, fmt, args);
    if (t_instance_cautions) Record(*t_instance_cautions, fmt, instance_args);
    va_end(instance_args);
}

}

Log::CautionScope::CautionScope(CautionRing& ring) : previous_(t_instance_cautions) { t_instance_cautions = &ring; }

Log::CautionScope::CautionScope(std::nullptr_t) : previous_(t_instance_cautions) { t_instance_cautions = nullptr; }

Log::CautionScope::~CautionScope() { t_instance_cautions = previous_; }

void Log::CopyRecentCautionsPostFreeze(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    const bool owned = log_detail::LogMutex().try_lock();
    CopyRing(g_process_cautions, out, out_size);
    if (owned) log_detail::LogMutex().unlock();
}

void Log::CopyCautions(const CautionRing& ring, char* out, size_t out_size) {
    std::lock_guard<std::mutex> lock(log_detail::LogMutex());
    CopyRing(ring, out, out_size);
}

}
