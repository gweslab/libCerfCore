#include "host_backtrace.h"

#define NOMINMAX
#include <windows.h>

namespace cerfcore::host_platform {

int CaptureBacktrace(void** frames, int max_frames) {
    return CaptureStackBackTrace(0, static_cast<DWORD>(max_frames), frames, nullptr);
}

}
