#pragma once

#include "host_entry_stat.h"
#include "host_shared_folder.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <sys/statvfs.h>

namespace cerfcore {

namespace host_platform {

std::string    SharedHostPath(const std::u16string& path);
SharedFsStatus SharedErrno(int err, const std::string& host_path);
SharedFsStatus SharedStatus(SharedFsError error, int err);

bool           FindSharedEntry(const std::string& dir, std::u16string_view name, std::string& found);
SharedFsStatus LocateSharedEntry(const std::string& host_path, std::string& actual);
void           SplitSharedPath(const std::string& host_path, std::string& dir, std::string& name);
std::string    JoinSharedPath(const std::string& dir, const std::string& name);
bool           SharedDirectoryExists(const std::string& dir);

SharedFsStatus DescribeSharedEntry(const std::string& path, SharedFsInfo& info);
SharedFsStatus DescribeSharedFd(int fd, SharedFsInfo& info);
void           DescribeUnattributedEntry(const EntryStat& st, SharedFsInfo& info);
SharedFsStatus StoreSharedAttributes(const std::string& path, uint8_t attributes);
bool           CopySharedAttributes(int from_fd, int to_fd);
void           SharedSpace(const struct statvfs& sv, uint64_t& total, uint64_t& available);

uint32_t       DosDateTimeOf(int64_t utc_seconds);
SharedFsStatus SharedWriteTimes(uint32_t dos, struct timespec (&times)[2]);

}

}
