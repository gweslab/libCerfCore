#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace cerfcore {

namespace host_platform {

enum class SharedFsError : uint8_t {
    kNone,
    kFileNotFound,
    kPathNotFound,
    kTooManyOpenFiles,
    kAccessDenied,
    kDirNotEmpty,
    kInvalidHandle,
    kNoMoreFiles,
    kWriteProtect,
    kWriteFault,
    kReadFault,
    kSharingViolation,
    kLockViolation,
    kDiskFull,
    kFileExists,
    kAlreadyExists,
    kInvalidName,
    kUnrepresentableTime,
    kUnmapped,
};

struct SharedFsStatus {
    SharedFsError error  = SharedFsError::kNone;
    uint32_t      native = 0;

    bool Ok() const { return error == SharedFsError::kNone; }
};

/* Linux msdos_fs.h ATTR_RO, ATTR_HIDDEN, ATTR_SYS, ATTR_DIR, ATTR_ARCH. */
enum SharedFsAttribute : uint8_t {
    kSharedReadOnly  = 0x01,
    kSharedHidden    = 0x02,
    kSharedSystem    = 0x04,
    kSharedDirectory = 0x10,
    kSharedArchive   = 0x20,
};

struct SharedFsInfo {
    uint8_t  attributes  = 0;
    uint64_t size        = 0;
    uint32_t write_time  = 0;
    uint32_t create_time = 0;
};

enum class SharedPathResult { kResolved, kRootUnavailable, kInvalidName };

enum class SharedFileAccess { kRead, kWrite, kReadWrite };
enum class SharedFileShare { kDenyReadWrite, kDenyWrite, kDenyRead, kDenyNone };

class SharedFsFind {
public:
    virtual ~SharedFsFind() = default;

    virtual SharedFsStatus Next(SharedFsInfo& info, std::u16string& name) = 0;
};

class SharedFsFile {
public:
    virtual ~SharedFsFile() = default;

    virtual SharedFsStatus ReadAt(uint64_t position, void* buf, uint32_t size, uint32_t& done)        = 0;
    virtual SharedFsStatus WriteAt(uint64_t position, const void* buf, uint32_t size, uint32_t& done) = 0;
    virtual SharedFsStatus TruncateAt(uint64_t position)                                              = 0;
    virtual SharedFsStatus Info(SharedFsInfo& info)                                                   = 0;
};

class HostSharedFolder {
public:
    virtual ~HostSharedFolder() = default;

    virtual SharedPathResult Resolve(std::u16string_view guest_name, std::u16string& out) = 0;

    virtual SharedFsStatus MakeDirectory(const std::u16string& path)                       = 0;
    virtual SharedFsStatus RemoveEmptyDirectory(const std::u16string& path)                = 0;
    virtual SharedFsStatus RemoveFile(const std::u16string& path)                          = 0;
    virtual SharedFsStatus Move(const std::u16string& from, const std::u16string& to)     = 0;
    virtual SharedFsStatus SetWriteTime(const std::u16string& path, uint32_t dos_time)     = 0;
    virtual SharedFsStatus SetAttributes(const std::u16string& path, uint8_t attributes)  = 0;
    virtual SharedFsStatus QueryInfo(const std::u16string& path, SharedFsInfo& info)       = 0;
    virtual SharedFsStatus QuerySpace(uint64_t& total, uint64_t& available)                = 0;
    virtual SharedFsStatus CreateNewFile(const std::u16string& path)                       = 0;

    virtual SharedFsStatus FindFirst(const std::u16string& pattern, std::unique_ptr<SharedFsFind>& find,
                                     SharedFsInfo& info, std::u16string& name) = 0;
    virtual SharedFsStatus Open(const std::u16string& path, SharedFileAccess access, SharedFileShare share,
                                std::unique_ptr<SharedFsFile>& file)           = 0;
};

std::unique_ptr<HostSharedFolder> OpenHostSharedFolder(const std::string& root);

}

}
