#include "host_backtrace.h"

#include <execinfo.h>

namespace cerfcore::host_platform {

int CaptureBacktrace(void** frames, int max_frames) { return ::backtrace(frames, max_frames); }

}
