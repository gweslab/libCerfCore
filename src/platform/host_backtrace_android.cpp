#include "host_backtrace.h"

#include <unwind.h>

#include <cstdint>

namespace cerfcore::host_platform {

namespace {

struct FrameWalk {
    void** frames;
    int    max_frames;
    int    count;
};

_Unwind_Reason_Code RecordFrame(_Unwind_Context* context, void* arg) {
    auto* walk = static_cast<FrameWalk*>(arg);
    if (walk->count >= walk->max_frames) return _URC_END_OF_STACK;
    const uintptr_t ip = _Unwind_GetIP(context);
    if (ip == 0) return _URC_END_OF_STACK;
    walk->frames[walk->count++] = reinterpret_cast<void*>(ip);
    return _URC_NO_REASON;
}

}

int CaptureBacktrace(void** frames, int max_frames) {
    FrameWalk walk{frames, max_frames, 0};
    _Unwind_Backtrace(RecordFrame, &walk);
    return walk.count;
}

}
