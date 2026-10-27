#include "host_install_directory.h"

#include <cerfcore/platform/host_app_data_directory.h>

namespace cerfcore {

namespace host_platform {

std::string InstallDirectory() { return host::AppDataDirectory(); }

}

}
