#include <cerfcore/core/host_file_read.h>

#include <cerfcore/core/host_file_bytes.h>
#include <cerfcore/platform/host_file.h>

namespace cerfcore {

HostFileRead ReadHostFileIfPresent(const std::string& path, std::vector<uint8_t>& out) {
    if (TryReadHostFileBytes(path, out)) return HostFileRead::kRead;
    return host_platform::HostPathExists(path) ? HostFileRead::kUnreadable : HostFileRead::kAbsent;
}

}
