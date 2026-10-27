#include "host_path_open.h"

#include "host_document_path.h"
#include "host_local_path_open.h"

#include <cerfcore/platform/host_descriptor.h>
#include <cerfcore/platform/host_documents.h>

#include <fcntl.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

namespace {

std::string DocumentMode(int flags) {
    std::string mode;
    switch (flags & O_ACCMODE) {
        case O_RDWR:   mode = "rw"; break;
        case O_WRONLY: mode = "w"; break;
        default:       mode = "r"; break;
    }
    if ((flags & O_TRUNC) != 0) mode += 't';
    if ((flags & O_APPEND) != 0) mode += 'a';
    return mode;
}

}

int OpenHostPath(const std::string& path, int flags, mode_t mode) {
    if (!IsHostDocument(path)) return OpenLocalPath(path, flags, mode);
    const int fd = host::OpenHostDocument(path, DocumentMode(flags).c_str());
    if (fd < 0) return -1;
    if ((flags & O_CLOEXEC) != 0 && !SetCloseOnExec(fd)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

bool StatHostPath(const std::string& path, struct stat& st) {
    if (!IsHostDocument(path)) return StatLocalPath(path, st);
    HostDocumentInfo info;
    if (!host::QueryHostDocument(path, info)) return false;
    st          = {};
    st.st_mode  = (info.directory ? S_IFDIR : S_IFREG) | (info.writable ? 0666u : 0444u);
    st.st_size  = static_cast<off_t>(info.size);
    st.st_mtime = static_cast<time_t>(info.modified_ms / 1000);
    return true;
}

}

}
