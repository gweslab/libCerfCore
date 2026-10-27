#pragma once

#include "host_shared_folder.h"

#include <cstdint>

namespace cerfcore {

namespace host_platform {

class FdSharedFile : public SharedFsFile {
public:
    explicit FdSharedFile(int fd) : fd_(fd) {}
    ~FdSharedFile() override;
    FdSharedFile(const FdSharedFile&)            = delete;
    FdSharedFile& operator=(const FdSharedFile&) = delete;

    SharedFsStatus ReadAt(uint64_t position, void* buf, uint32_t size, uint32_t& done) override;
    SharedFsStatus WriteAt(uint64_t position, const void* buf, uint32_t size, uint32_t& done) override;
    SharedFsStatus TruncateAt(uint64_t position) override;

protected:
    int Fd() const { return fd_; }

private:
    int fd_;
};

}

}
