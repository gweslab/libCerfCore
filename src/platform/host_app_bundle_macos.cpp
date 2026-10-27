#include "host_app_bundle.h"

#include <cerfcore/core/path_split.h>

#include <cstring>

namespace cerfcore {

namespace host_platform {

size_t AppBundleLength(const char* executable_path, size_t length) {
    static constexpr char kInBundle[]      = ".app/Contents/MacOS/";
    constexpr size_t      kInBundleLength  = sizeof(kInBundle) - 1;
    constexpr size_t      kExtensionLength = 4;
    const size_t          cut              = PathNameOffset(executable_path, length, kPosixPathSeparators<char>);
    if (cut < kInBundleLength ||
        std::memcmp(executable_path + cut - kInBundleLength, kInBundle, kInBundleLength) != 0) {
        return 0;
    }
    return cut - kInBundleLength + kExtensionLength;
}

}

}
