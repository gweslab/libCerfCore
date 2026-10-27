#include <cerfcore/core/host_file_bytes.h>

#include "../platform/host_whole_file.h"

#include <cerfcore/platform/host_file.h>

#include <algorithm>

namespace cerfcore {

bool TryReadHostFileBytes(const std::string& path, std::vector<uint8_t>& out) {
    return host_platform::ReadWholeHostFile(path, out);
}

std::vector<uint8_t> ReadHostFileBytes(const std::string& path) {
    std::vector<uint8_t> bytes;
    TryReadHostFileBytes(path, bytes);
    return bytes;
}

HostWriteResult WriteHostFileBytes(const std::string& path, const uint8_t* data, size_t size) {
    host_platform::HostFile file;
    if (!file.Open(path, host_platform::FileMode::kWriteNew)) return HostWriteResult::kOpenFailed;
    constexpr size_t kChunk = 0x40000000u;
    while (size != 0) {
        const uint32_t want = static_cast<uint32_t>(std::min(size, kChunk));
        uint32_t done = 0;
        if (!file.Write(data, want, done) || done != want) return HostWriteResult::kShortWrite;
        data += done;
        size -= done;
    }
    return HostWriteResult::kOk;
}

bool HostFileNonEmpty(const std::string& path) {
    uint64_t size = 0;
    return host_platform::HostRegularFileSize(path, size) && size != 0;
}

}
