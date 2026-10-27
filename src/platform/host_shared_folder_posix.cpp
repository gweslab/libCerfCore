#include "host_shared_folder.h"

#include "host_posix_shared_folder.h"

namespace cerfcore {

namespace host_platform {

std::unique_ptr<HostSharedFolder> OpenHostSharedFolder(const std::string& root) {
    return OpenPosixSharedFolder(root);
}

}

}
