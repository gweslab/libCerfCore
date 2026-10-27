#include "host_entry_statx.h"

#include "host_entry_fields.h"

#include <fcntl.h>

namespace cerfcore {

namespace host_platform {

bool ReadEntryStatx(int dirfd, const char* path, int flags, EntryStat& out) {
    struct statx sx {};
    if (statx(dirfd, path, flags | AT_STATX_SYNC_AS_STAT, STATX_BASIC_STATS | STATX_BTIME, &sx) != 0) return false;
    const int64_t mtime = sx.stx_mtime.tv_sec;
    const int64_t btime = (sx.stx_mask & STATX_BTIME) != 0u ? sx.stx_btime.tv_sec : mtime;
    FillEntryFields(out, sx.stx_mode, sx.stx_size, mtime, btime);
    return true;
}

}

}
