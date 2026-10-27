#pragma once

#include "host_entry_stat.h"

#if defined(__ANDROID__)
#define CERF_STATX_INTRODUCED __INTRODUCED_IN(30)
#else
#define CERF_STATX_INTRODUCED
#endif

namespace cerfcore {

namespace host_platform {

bool ReadEntryStatx(int dirfd, const char* path, int flags, EntryStat& out) CERF_STATX_INTRODUCED;

}

}
