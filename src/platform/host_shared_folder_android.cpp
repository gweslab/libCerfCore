#include "host_shared_folder.h"

#include "host_document_path.h"
#include "host_document_shared_folder.h"
#include "host_posix_shared_folder.h"

namespace cerfcore {

namespace host_platform {

std::unique_ptr<HostSharedFolder> OpenHostSharedFolder(const std::string& root) {
    if (IsHostDocument(root)) return OpenDocumentSharedFolder(root);
    return OpenPosixSharedFolder(root);
}

}

}
