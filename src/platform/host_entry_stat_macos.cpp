#include "host_entry_stat.h"

#include "host_entry_fields.h"

namespace cerfcore {

namespace host_platform {

namespace {

void FillEntryStat(const struct stat& st, EntryStat& out) {
    FillEntryFields(out, st.st_mode, static_cast<uint64_t>(st.st_size), st.st_mtimespec.tv_sec,
                    st.st_birthtimespec.tv_sec);
}

}

bool ReadEntryStat(const char* path, EntryStat& out) {
    struct stat st {};
    if (stat(path, &st) != 0) return false;
    FillEntryStat(st, out);
    return true;
}

bool ReadEntryStatFd(int fd, EntryStat& out) {
    struct stat st {};
    if (fstat(fd, &st) != 0) return false;
    FillEntryStat(st, out);
    return true;
}

timespec StatModifiedTime(const struct stat& st) { return st.st_mtimespec; }

}

}
