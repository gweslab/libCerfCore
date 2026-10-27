#include <cerfcore/platform/host_path_name.h>

#include "host_document_path.h"

namespace cerfcore {

namespace host_platform {

std::string HostLocationName(const std::string& path) {
    return IsHostDocument(path) ? HostPathDisplayName(path) : path;
}

}

}
