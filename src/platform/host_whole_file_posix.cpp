#include "host_whole_file.h"

#include "host_path_open.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace cerfcore {

namespace host_platform {

bool ReadWholeHostFile(const std::string& path, std::vector<uint8_t>& out) {
    const int fd = OpenHostPath(path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) return false;
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        return false;
    }
    out.resize(static_cast<size_t>(st.st_size));
    size_t done = 0;
    while (done < out.size()) {
        const ssize_t n = ::read(fd, out.data() + done, out.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) {
            ::close(fd);
            return false;
        }
        if (n == 0) break;
        done += static_cast<size_t>(n);
    }
    ::close(fd);
    out.resize(done);
    return true;
}

}

}
