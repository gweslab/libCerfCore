#include "host_entry_stat.h"

#include "host_entry_at.h"

#include <fcntl.h>

namespace cerfcore {

namespace host_platform {

bool ReadEntryStat(const char* path, EntryStat& out) { return ReadEntryAt(AT_FDCWD, path, 0, out); }

bool ReadEntryStatFd(int fd, EntryStat& out) { return ReadEntryAt(fd, "", AT_EMPTY_PATH, out); }

timespec StatModifiedTime(const struct stat& st) { return st.st_mtim; }

}

}
