#include "host_posix_shared_folder.h"

#include "host_entry_stat.h"
#include "host_fd_shared_file.h"
#include "host_rename_entry.h"
#include "host_share_modes.h"
#include "host_shared_entry.h"
#include "host_shared_path_resolve.h"
#include "host_win32_name_rules.h"

#include <cerfcore/core/path_segments.h>
#include <cerfcore/core/path_split.h>
#include <cerfcore/core/utf16.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cerfcore {

namespace host_platform {

namespace {

bool NormalizeRoot(const std::string& root, std::vector<std::string>& segments) {
    if (root.empty()) return false;
    std::string path = root;
    if (path[0] != '/') {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) return false;
        path = std::string(cwd) + "/" + path;
    }
    for (const std::string& segment : PathSegments(path, 0, kPosixPathSeparators<char>)) {
        if (segment == ".") continue;
        if (segment == "..") {
            if (!segments.empty()) segments.pop_back();
            continue;
        }
        segments.push_back(segment);
    }
    return !segments.empty();
}

SharedFsStatus NameStatus(const std::string& dir, const std::string& name, std::u16string& name16) {
    if (!SharedDirectoryExists(dir)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
    name16 = Utf8ToUtf16(name);
    if (!IsWin32FileName(name16)) return SharedStatus(SharedFsError::kInvalidName, EINVAL);
    return {};
}

bool CopyContents(int in, int out) {
    char buf[65536];
    for (;;) {
        const ssize_t got = read(in, buf, sizeof(buf));
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) return got == 0;
        for (ssize_t done = 0; done < got;) {
            const ssize_t n = write(out, buf + done, static_cast<size_t>(got - done));
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) return false;
            done += n;
        }
    }
}

SharedFsStatus MoveAcrossDevices(const std::string& src, const std::string& dst) {
    struct stat st {};
    if (stat(src.c_str(), &st) != 0) return SharedErrno(errno, src);
    if (!S_ISREG(st.st_mode)) return SharedStatus(SharedFsError::kUnmapped, EXDEV);
    const int in = open(src.c_str(), O_RDONLY | O_CLOEXEC);
    if (in < 0) return SharedErrno(errno, src);
    const mode_t mode = st.st_mode & 07777u;
    const int    out  = open(dst.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode | S_IWUSR);
    if (out < 0) {
        const int err = errno;
        close(in);
        return err == EEXIST ? SharedStatus(SharedFsError::kAlreadyExists, err) : SharedErrno(err, dst);
    }
    bool ok = CopyContents(in, out) && CopySharedAttributes(in, out);
    int  err = ok ? 0 : errno;
    if (ok) {
        const struct timespec times[2] = {{0, UTIME_OMIT}, StatModifiedTime(st)};
        ok = futimens(out, times) == 0 && fchmod(out, mode) == 0;
        if (!ok) err = errno;
    }
    close(in);
    if (close(out) != 0 && ok) {
        ok  = false;
        err = errno;
    }
    if (!ok) {
        unlink(dst.c_str());
        return SharedErrno(err, dst);
    }
    if (unlink(src.c_str()) != 0) return SharedErrno(errno, src);
    return {};
}

bool SameInode(const std::string& a, const std::string& b) {
    struct stat sa {};
    struct stat sb {};
    return lstat(a.c_str(), &sa) == 0 && lstat(b.c_str(), &sb) == 0 && sa.st_dev == sb.st_dev &&
           sa.st_ino == sb.st_ino;
}

class PosixSharedFind : public SharedFsFind {
public:
    explicit PosixSharedFind(std::string dir) : dir_(std::move(dir)) {}
    ~PosixSharedFind() override {
        if (hold_fd_ >= 0) close(hold_fd_);
    }
    PosixSharedFind(const PosixSharedFind&)            = delete;
    PosixSharedFind& operator=(const PosixSharedFind&) = delete;

    SharedFsStatus Collect(const std::u16string& mask) {
        SharedFsStatus held = HoldSharedDirectory(dir_, hold_fd_);
        if (!held.Ok()) return held;
        if (!HasWin32Wildcard(mask)) {
            std::string found;
            if (!FindSharedEntry(dir_, mask, found)) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
            names_.emplace_back(found, Utf8ToUtf16(found));
            return {};
        }
        DIR* d = opendir(dir_.c_str());
        if (d == nullptr) return SharedErrno(errno, dir_);
        std::vector<std::string> entries;
        while (const dirent* e = readdir(d)) entries.emplace_back(e->d_name);
        closedir(d);
        for (Win32NameMatch& match : MatchWin32Names(entries, mask)) {
            names_.emplace_back(std::move(entries[match.index]), std::move(match.name));
        }
        return {};
    }

    SharedFsStatus Next(SharedFsInfo& info, std::u16string& name) override {
        while (next_ < names_.size()) {
            const auto&          entry = names_[next_++];
            const SharedFsStatus st    = DescribeSharedEntry(JoinSharedPath(dir_, entry.first), info);
            if (st.Ok()) {
                name = entry.second;
                return {};
            }
            if (st.native != ENOENT) return st;
        }
        return SharedStatus(SharedFsError::kNoMoreFiles, ENOENT);
    }

private:
    std::string                                         dir_;
    std::vector<std::pair<std::string, std::u16string>> names_;
    size_t                                              next_    = 0;
    int                                                 hold_fd_ = -1;
};

class PosixSharedFile : public FdSharedFile {
public:
    PosixSharedFile(int fd, int lock_fd) : FdSharedFile(fd), lock_fd_(lock_fd) {}
    ~PosixSharedFile() override {
        if (lock_fd_ != Fd()) close(lock_fd_);
    }

    SharedFsStatus Info(SharedFsInfo& info) override { return DescribeSharedFd(Fd(), info); }

private:
    int lock_fd_;
};

class PosixSharedFolder : public HostSharedFolder {
public:
    explicit PosixSharedFolder(std::string root) : root_(std::move(root)) {}

    SharedPathResult Resolve(std::u16string_view guest_name, std::u16string& out) override {
        std::vector<std::string> root_segments;
        if (!NormalizeRoot(root_, root_segments)) return SharedPathResult::kRootUnavailable;
        std::vector<Win32PathSegment> base;
        std::string                   path;
        for (const std::string& s : root_segments) {
            base.push_back(Win32PathSegment{Utf8ToUtf16(s), false});
            path += "/" + s;
        }
        return ResolveSharedGuestPath(
            base, std::move(path), guest_name,
            [](const std::string& dir, const std::u16string& name) {
                std::string found;
                return FindSharedEntry(dir, name, found) ? found : Utf16ToUtf8(name);
            },
            out);
    }

    SharedFsStatus MakeDirectory(const std::u16string& path) override {
        std::string dir;
        std::string name;
        SplitSharedPath(SharedHostPath(path), dir, name);
        std::u16string name16;
        const SharedFsStatus st = NameStatus(dir, name, name16);
        if (!st.Ok()) return st;
        std::string found;
        if (FindSharedEntry(dir, name16, found)) return SharedStatus(SharedFsError::kAlreadyExists, EEXIST);
        const std::string target = JoinSharedPath(dir, name);
        if (mkdir(target.c_str(), 0777) != 0) {
            return errno == EEXIST ? SharedStatus(SharedFsError::kAlreadyExists, errno)
                                   : SharedErrno(errno, target);
        }
        return {};
    }

    SharedFsStatus RemoveEmptyDirectory(const std::u16string& path) override {
        std::string    actual;
        SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
        if (!st.Ok()) return st;
        SharedFsInfo info;
        st = DescribeSharedEntry(actual, info);
        if (!st.Ok()) return st;
        if ((info.attributes & kSharedDirectory) == 0u) return SharedStatus(SharedFsError::kUnmapped, ENOTDIR);
        if ((info.attributes & kSharedReadOnly) != 0u) return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        if (rmdir(actual.c_str()) != 0) {
            return (errno == ENOTEMPTY || errno == EEXIST) ? SharedStatus(SharedFsError::kDirNotEmpty, errno)
                                                           : SharedErrno(errno, actual);
        }
        return {};
    }

    SharedFsStatus RemoveFile(const std::u16string& path) override {
        std::string    actual;
        SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
        if (!st.Ok()) return st;
        SharedFsInfo info;
        st = DescribeSharedEntry(actual, info);
        if (!st.Ok()) return st;
        if ((info.attributes & (kSharedDirectory | kSharedReadOnly)) != 0u) {
            return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        }
        st = CheckSharedEntryFree(actual);
        if (!st.Ok()) return st;
        if (unlink(actual.c_str()) != 0) return SharedErrno(errno, actual);
        return {};
    }

    SharedFsStatus Move(const std::u16string& from, const std::u16string& to) override {
        std::string    src;
        SharedFsStatus st = LocateSharedEntry(SharedHostPath(from), src);
        if (!st.Ok()) return st;
        st = CheckSharedEntryFree(src);
        if (!st.Ok()) return st;
        std::string dir;
        std::string name;
        SplitSharedPath(SharedHostPath(to), dir, name);
        std::u16string name16;
        st = NameStatus(dir, name, name16);
        if (!st.Ok()) return st;
        const std::string dst = JoinSharedPath(dir, name);
        std::string       found;
        const bool        renames_self = FindSharedEntry(dir, name16, found) &&
                                         SameInode(JoinSharedPath(dir, found), src);
        if (!found.empty() && !renames_self) return SharedStatus(SharedFsError::kAlreadyExists, EEXIST);
        if (RenameEntry(src.c_str(), dst.c_str(), !renames_self) == 0) return {};
        if (RenameNoReplaceUnsupported(errno) && rename(src.c_str(), dst.c_str()) == 0) return {};
        if (errno == EEXIST) return SharedStatus(SharedFsError::kAlreadyExists, errno);
        if (errno == EXDEV) return MoveAcrossDevices(src, dst);
        return SharedErrno(errno, dst);
    }

    SharedFsStatus SetWriteTime(const std::u16string& path, uint32_t dos_time) override {
        struct timespec times[2];
        SharedFsStatus  st = SharedWriteTimes(dos_time, times);
        if (!st.Ok()) return st;
        std::string actual;
        st = LocateSharedEntry(SharedHostPath(path), actual);
        if (!st.Ok()) return st;
        if (utimensat(AT_FDCWD, actual.c_str(), times, 0) != 0) return SharedErrno(errno, actual);
        return {};
    }

    SharedFsStatus SetAttributes(const std::u16string& path, uint8_t attributes) override {
        std::string    actual;
        SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
        if (!st.Ok()) return st;
        return StoreSharedAttributes(actual, attributes);
    }

    SharedFsStatus QueryInfo(const std::u16string& path, SharedFsInfo& info) override {
        std::string    actual;
        SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
        if (!st.Ok()) return st;
        return DescribeSharedEntry(actual, info);
    }

    SharedFsStatus QuerySpace(uint64_t& total, uint64_t& available) override {
        struct statvfs sv {};
        if (statvfs(root_.c_str(), &sv) != 0) return SharedErrno(errno, root_);
        SharedSpace(sv, total, available);
        return {};
    }

    SharedFsStatus CreateNewFile(const std::u16string& path) override {
        std::string dir;
        std::string name;
        SplitSharedPath(SharedHostPath(path), dir, name);
        std::u16string name16;
        const SharedFsStatus st = NameStatus(dir, name, name16);
        if (!st.Ok()) return st;
        std::string found;
        if (FindSharedEntry(dir, name16, found)) return SharedStatus(SharedFsError::kFileExists, EEXIST);
        const std::string target = JoinSharedPath(dir, name);
        const int         fd = open(target.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOCTTY, 0666);
        if (fd < 0) return SharedErrno(errno, target);
        close(fd);
        return {};
    }

    SharedFsStatus FindFirst(const std::u16string& pattern, std::unique_ptr<SharedFsFind>& find,
                             SharedFsInfo& info, std::u16string& name) override {
        const std::string    host = SharedHostPath(pattern);
        const std::string    dir  = ParentPath(host, kPosixPathSeparators<char>);
        const std::u16string mask =
            Utf8ToUtf16(host.substr(PathNameOffset(host.data(), host.size(), kPosixPathSeparators<char>)));
        if (mask.empty()) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
        if (!SharedDirectoryExists(dir)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
        auto           opened = std::make_unique<PosixSharedFind>(dir);
        SharedFsStatus st     = opened->Collect(mask);
        if (!st.Ok()) return st;
        st = opened->Next(info, name);
        if (st.error == SharedFsError::kNoMoreFiles) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
        if (st.Ok()) find = std::move(opened);
        return st;
    }

    SharedFsStatus Open(const std::u16string& path, SharedFileAccess access, SharedFileShare share,
                        std::unique_ptr<SharedFsFile>& file) override {
        std::string    actual;
        SharedFsStatus st = LocateSharedEntry(SharedHostPath(path), actual);
        if (!st.Ok()) return st;
        int flags = O_CLOEXEC | O_NOCTTY | O_NONBLOCK;
        switch (access) {
            case SharedFileAccess::kRead:      flags |= O_RDONLY; break;
            case SharedFileAccess::kWrite:     flags |= O_WRONLY; break;
            case SharedFileAccess::kReadWrite: flags |= O_RDWR; break;
        }
        const int fd = open(actual.c_str(), flags);
        if (fd < 0) return SharedErrno(errno, actual);
        struct stat sb {};
        if (fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode) || fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) != 0) {
            close(fd);
            return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        }
        const int lock_fd = access == SharedFileAccess::kWrite
                                ? open(actual.c_str(), O_RDONLY | O_CLOEXEC | O_NOCTTY | O_NONBLOCK)
                                : fd;
        if (lock_fd < 0) {
            const int err = errno;
            close(fd);
            return SharedErrno(err, actual);
        }
        st = ClaimShareMode(lock_fd, actual, ShareClaimFor(access, share));
        if (!st.Ok()) {
            if (lock_fd != fd) close(lock_fd);
            close(fd);
            return st;
        }
        file = std::make_unique<PosixSharedFile>(fd, lock_fd);
        return {};
    }

private:
    std::string root_;
};

}

std::unique_ptr<HostSharedFolder> OpenPosixSharedFolder(const std::string& root) {
    return std::make_unique<PosixSharedFolder>(root);
}

}

}
