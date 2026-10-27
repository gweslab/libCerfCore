#include "host_entry_at.h"

#include "host_entry_fields.h"
#include "host_entry_statx.h"

namespace cerfcore {

namespace host_platform {

bool ReadEntryAt(int dirfd, const char* path, int flags, EntryStat& out) {
    if (__builtin_available(android 30, *)) return ReadEntryStatx(dirfd, path, flags, out);
    struct stat st {};
    if (fstatat(dirfd, path, &st, flags) != 0) return false;
    FillEntryFields(out, st.st_mode, static_cast<uint64_t>(st.st_size), st.st_mtim.tv_sec, st.st_mtim.tv_sec);
    return true;
}

}

}
