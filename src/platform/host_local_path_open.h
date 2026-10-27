#pragma once

#include <string>
#include <sys/stat.h>

namespace cerfcore {

namespace host_platform {

int  OpenLocalPath(const std::string& path, int flags, mode_t mode);
bool StatLocalPath(const std::string& path, struct stat& st);

}

}
