#include "host_rename_entry.h"

#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

int RenameEntry(const char* from, const char* to, bool no_replace) {
    return static_cast<int>(
        syscall(SYS_renameat2, AT_FDCWD, from, AT_FDCWD, to, no_replace ? RENAME_NOREPLACE : 0u));
}

bool RenameNoReplaceUnsupported(int err) { return err == EINVAL; }

}

}
