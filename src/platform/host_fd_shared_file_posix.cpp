#include "host_fd_shared_file.h"

#include "host_shared_entry.h"

#include <cerrno>
#include <sys/types.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

SharedFsStatus IoStatus(int err, SharedFsError fault) {
    switch (err) {
        case EBADF:
        case EINVAL: return SharedStatus(SharedFsError::kAccessDenied, err);
        case EIO:    return SharedStatus(fault, err);
        case ENOSPC:
        case EDQUOT: return SharedStatus(SharedFsError::kDiskFull, err);
        default:     return SharedStatus(SharedFsError::kUnmapped, err);
    }
}

}

FdSharedFile::~FdSharedFile() { close(fd_); }

SharedFsStatus FdSharedFile::ReadAt(uint64_t position, void* buf, uint32_t size, uint32_t& done) {
    auto*    out   = static_cast<uint8_t*>(buf);
    uint32_t total = 0;
    while (total < size) {
        const ssize_t n = pread(fd_, out + total, size - total, static_cast<off_t>(position + total));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return IoStatus(errno, SharedFsError::kReadFault);
        if (n == 0) break;
        total += static_cast<uint32_t>(n);
    }
    done = total;
    return {};
}

SharedFsStatus FdSharedFile::WriteAt(uint64_t position, const void* buf, uint32_t size, uint32_t& done) {
    const auto* in    = static_cast<const uint8_t*>(buf);
    uint32_t    total = 0;
    while (total < size) {
        const ssize_t n = pwrite(fd_, in + total, size - total, static_cast<off_t>(position + total));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return IoStatus(n < 0 ? errno : EIO, SharedFsError::kWriteFault);
        total += static_cast<uint32_t>(n);
    }
    done = total;
    return {};
}

SharedFsStatus FdSharedFile::TruncateAt(uint64_t position) {
    if (ftruncate(fd_, static_cast<off_t>(position)) != 0) return IoStatus(errno, SharedFsError::kWriteFault);
    return {};
}

}

}
