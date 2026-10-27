#include <cerfcore/platform/host_path_name.h>

#include "host_document_path.h"
#include "host_path_tail.h"

#include <cerfcore/core/path_split.h>
#include <cerfcore/platform/host_documents.h>

namespace cerfcore {

namespace host_platform {

std::string HostPathDisplayName(const std::string& path) {
    if (IsHostDocument(path)) {
        HostDocumentInfo info;
        return host::QueryHostDocument(path, info) ? info.name : path;
    }
    return PathTail(path, kPosixPathSeparators<char>);
}

}

}
