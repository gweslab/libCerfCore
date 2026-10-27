#include "host_install_directory.h"

#include <cerfcore/core/path_split.h>
#include <cerfcore/platform/host_process.h>

namespace cerfcore {

namespace host_platform {

std::string InstallDirectory() {
    const std::string executable = ExecutablePath();
    return executable.substr(0, PathNameOffset(executable.data(), executable.size(), kWindowsPathSeparators<char>));
}

}

}
