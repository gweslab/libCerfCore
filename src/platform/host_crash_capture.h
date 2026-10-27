#pragma once

#include <cstddef>
#include <cstdint>

namespace cerfcore {

namespace host_platform {

struct CrashThreadState {
    long      tid;
    uintptr_t pc;
    uintptr_t sp;
    uintptr_t fp;
};

long CrashThreadId();
void CrashContextRegisters(const void* ucontext, uintptr_t& pc, uintptr_t& sp, uintptr_t& fp);
int  CrashCaptureOtherThreads(CrashThreadState* out, int capacity);

}

}
