#include "host_document_path.h"

namespace cerfcore {

namespace host_platform {

namespace {

constexpr char kDocumentScheme[] = "content://";

}

bool IsHostDocument(const std::string& path) { return path.rfind(kDocumentScheme, 0) == 0; }

}

}
