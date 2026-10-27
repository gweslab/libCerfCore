#include "host_local_path_open.h"

#include <fcntl.h>

namespace cerfcore {

namespace host_platform {

int OpenLocalPath(const std::string& path, int flags, mode_t mode) { return ::open(path.c_str(), flags, mode); }

bool StatLocalPath(const std::string& path, struct stat& st) { return ::stat(path.c_str(), &st) == 0; }

}

}
