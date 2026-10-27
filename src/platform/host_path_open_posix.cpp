#include "host_path_open.h"

#include "host_local_path_open.h"

namespace cerfcore {

namespace host_platform {

int OpenHostPath(const std::string& path, int flags, mode_t mode) { return OpenLocalPath(path, flags, mode); }

bool StatHostPath(const std::string& path, struct stat& st) { return StatLocalPath(path, st); }

}

}
