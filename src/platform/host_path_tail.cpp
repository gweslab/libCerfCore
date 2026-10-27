#include "host_path_tail.h"

#include <cerfcore/core/path_split.h>

namespace cerfcore {

namespace host_platform {

std::string PathTail(const std::string& path, const char* separators) {
    return path.substr(PathNameOffset(path.data(), path.size(), separators));
}

}

}
