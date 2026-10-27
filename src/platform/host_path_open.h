#pragma once

#include <string>
#include <sys/stat.h>

namespace cerfcore {

namespace host_platform {

int  OpenHostPath(const std::string& path, int flags, mode_t mode);
bool StatHostPath(const std::string& path, struct stat& st);

}

}
