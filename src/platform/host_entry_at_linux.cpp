#include "host_entry_at.h"

#include "host_entry_statx.h"

namespace cerfcore {

namespace host_platform {

bool ReadEntryAt(int dirfd, const char* path, int flags, EntryStat& out) {
    return ReadEntryStatx(dirfd, path, flags, out);
}

}

}
