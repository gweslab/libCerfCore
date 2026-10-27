#include "host_install_directory.h"

#include <cerfcore/core/path_split.h>
#include <cerfcore/platform/host_process.h>

#include "host_app_bundle.h"

namespace cerfcore {

namespace host_platform {

std::string InstallDirectory() {
    std::string application = ExecutablePath();
#if defined(__APPLE__)
    const size_t bundle = AppBundleLength(application.data(), application.size());
    if (bundle != 0) application.resize(bundle);
#endif
    return application.substr(0, PathNameOffset(application.data(), application.size(), kPosixPathSeparators<char>));
}

}

}
