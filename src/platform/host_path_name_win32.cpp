#include <cerfcore/platform/host_path_name.h>

#include "host_path_tail.h"

#include <cerfcore/core/path_split.h>

namespace cerfcore {

namespace host_platform {

std::string HostPathDisplayName(const std::string& path) { return PathTail(path, kWindowsPathSeparators<char>); }

}

}
