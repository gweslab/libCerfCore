#include "host_document_handles.h"

#include "host_entry_stat.h"
#include "host_shared_entry.h"

#include <cerrno>

namespace cerfcore {

namespace host_platform {

uint64_t DocumentOpenTable::Add(Entry entry) {
    const uint64_t id = ++next_id_;
    entries_.emplace(id, std::move(entry));
    return id;
}

void DocumentOpenTable::Remove(uint64_t id) { entries_.erase(id); }

bool DocumentOpenTable::Conflicts(const std::string& uri, const ShareClaim& claim) const {
    for (const auto& [id, e] : entries_) {
        if (e.uri != uri) continue;
        if ((claim.read && !e.claim.share_read) || (claim.write && !e.claim.share_write) ||
            (!claim.share_read && e.claim.read) || (!claim.share_write && e.claim.write)) {
            return true;
        }
    }
    return false;
}

bool DocumentOpenTable::IsOpen(const std::string& uri) const {
    for (const auto& [id, e] : entries_) {
        if (e.uri == uri) return true;
    }
    return false;
}

bool DocumentOpenTable::IsOpenAtOrUnder(const std::string& uri, const std::string& path) const {
    const std::string prefix = path + "/";
    for (const auto& [id, e] : entries_) {
        if (e.uri == uri || e.path.compare(0, prefix.size(), prefix) == 0) return true;
    }
    return false;
}

DocumentSharedFind::DocumentSharedFind(std::shared_ptr<DocumentOpenTable> table, uint64_t id, std::vector<Item> items)
    : table_(std::move(table)), id_(id), items_(std::move(items)) {}

DocumentSharedFind::~DocumentSharedFind() { table_->Remove(id_); }

SharedFsStatus DocumentSharedFind::Next(SharedFsInfo& info, std::u16string& name) {
    if (next_ >= items_.size()) return SharedStatus(SharedFsError::kNoMoreFiles, ENOENT);
    const Item& item = items_[next_++];
    name             = item.first;
    info             = item.second;
    return {};
}

DocumentSharedFile::DocumentSharedFile(int fd, std::shared_ptr<DocumentOpenTable> table, uint64_t id, bool writable)
    : FdSharedFile(fd), table_(std::move(table)), id_(id), writable_(writable) {}

DocumentSharedFile::~DocumentSharedFile() { table_->Remove(id_); }

SharedFsStatus DocumentSharedFile::Info(SharedFsInfo& info) {
    EntryStat st;
    if (!ReadEntryStatFd(Fd(), st)) return SharedStatus(SharedFsError::kInvalidHandle, errno);
    st.writable = writable_;
    st.btime    = st.mtime;
    DescribeUnattributedEntry(st, info);
    return {};
}

}

}
