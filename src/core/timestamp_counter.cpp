#include "timestamp_counter.h"

#if defined(_M_IX86) || defined(_M_X64)
#include <intrin.h>
#elif defined(__i386__) || defined(__x86_64__)
#include <x86intrin.h>
#elif !(defined(__aarch64__) && defined(__linux__))
#include <chrono>
#endif

namespace cerfcore {

uint64_t ReadTimestampCounter() {
#if defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__)
    return __rdtsc();
#elif defined(__aarch64__) && defined(__linux__)
    /* ARM DDI 0487 M.d D24.10.26 CNTVCT_EL0; Linux arm64 traps cntvct_read_handler. */
    uint64_t count;
    asm volatile("mrs %0, cntvct_el0" : "=r"(count));
    return count;
#else
    return static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

}
