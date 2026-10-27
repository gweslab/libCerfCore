#include "host_whole_file.h"

#include <cerfcore/core/host_fs_path.h>

#include <fstream>

namespace cerfcore {

namespace host_platform {

bool ReadWholeHostFile(const std::string& path, std::vector<uint8_t>& out) {
    std::ifstream f(HostFsPath(path), std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;
    const std::streamoff sz = static_cast<std::streamoff>(f.tellg());
    if (sz < 0) return false;
    out.resize(static_cast<size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(out.data()), sz);
    if (f.bad()) return false;
    out.resize(static_cast<size_t>(f.gcount()));
    return true;
}

}

}
