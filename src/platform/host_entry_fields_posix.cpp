#include "host_entry_fields.h"

namespace cerfcore {

namespace host_platform {

void FillEntryFields(EntryStat& out, unsigned mode, uint64_t size, int64_t mtime, int64_t btime) {
    out.is_dir   = S_ISDIR(mode);
    out.writable = (mode & 0222u) != 0u;
    out.size     = size;
    out.mtime    = mtime;
    out.btime    = btime;
}

}

}
