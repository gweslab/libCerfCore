#include <cerfcore/platform/host_file.h>

#include <cerfcore/platform/host_error.h>
#include <cerfcore/platform/host_process.h>

#include <algorithm>
#include <limits>

namespace cerfcore {

namespace host_platform {

bool ReplaceHostFileContents(const std::string& path, std::string_view bytes, bool write_through,
                             uint32_t& error) {
    const std::string temp = path + "." + std::to_string(CurrentProcessId()) + ".tmp";
    HostFile          file;
    if (!file.Open(temp, FileMode::kWriteNew)) {
        error = LastErrorCode();
        return false;
    }
    bool written = true;
    for (size_t at = 0; written && at < bytes.size();) {
        const uint32_t chunk = static_cast<uint32_t>(
            std::min<size_t>(bytes.size() - at, std::numeric_limits<uint32_t>::max()));
        uint32_t done = 0;
        written       = file.Write(bytes.data() + at, chunk, done) && done == chunk;
        at += chunk;
    }
    error = written ? 0 : LastErrorCode();
    file.Close();
    if (written && ReplaceHostFile(temp, path, write_through)) return true;
    if (written) error = LastErrorCode();
    uint32_t ignored = 0;
    DeleteHostFile(temp, ignored);
    return false;
}

}

}
