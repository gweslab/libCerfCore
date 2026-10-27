#pragma once

#include "host_shared_folder.h"

#include <memory>
#include <string>

namespace cerfcore {

namespace host_platform {

std::unique_ptr<HostSharedFolder> OpenPosixSharedFolder(const std::string& root);

}

}
