#include "host_document_shared_folder.h"

#include "host_document_handles.h"
#include "host_entry_stat.h"
#include "host_share_modes.h"
#include "host_shared_entry.h"
#include "host_shared_path_resolve.h"
#include "host_win32_name_rules.h"

#include <cerfcore/core/log.h>
#include <cerfcore/core/path_segments.h>
#include <cerfcore/core/utf16.h>
#include <cerfcore/platform/host_descriptor.h>
#include <cerfcore/platform/host_documents.h>

#include <cerrno>
#include <optional>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace cerfcore {

namespace host_platform {

namespace {

constexpr char kMoveSuffix[] = ".cerfmove";

struct DocumentNode {
    std::string      uri;
    std::string      path;
    HostDocumentInfo info;
};

SharedFsStatus DocumentError(int err) {
    if (err == ENOENT) return SharedStatus(SharedFsError::kFileNotFound, err);
    return SharedErrno(err, std::string());
}

SharedFsInfo Describe(const HostDocumentInfo& doc) {
    EntryStat st;
    st.is_dir   = doc.directory;
    st.writable = doc.writable;
    st.size     = doc.size;
    st.mtime    = doc.modified_ms / 1000;
    st.btime    = st.mtime;
    SharedFsInfo info;
    DescribeUnattributedEntry(st, info);
    return info;
}

const HostDocumentEntry* Match(const std::vector<HostDocumentEntry>& children, std::u16string_view name) {
    const std::string        exact  = Utf16ToUtf8(std::u16string(name));
    const HostDocumentEntry* folded = nullptr;
    for (const HostDocumentEntry& child : children) {
        std::u16string child16;
        if (!UsableWin32Name(child.info.name, child16)) continue;
        if (child.info.name == exact) return &child;
        if (folded == nullptr && SameWin32Name(child16, name)) folded = &child;
    }
    return folded;
}

DocumentNode ChildNode(const DocumentNode& parent, const HostDocumentEntry& child) {
    return DocumentNode{child.uri, parent.path + "/" + child.info.name, child.info};
}

class DocumentSharedFolder : public HostSharedFolder {
public:
    explicit DocumentSharedFolder(std::string root)
        : root_(std::move(root)), table_(std::make_shared<DocumentOpenTable>()) {}

    SharedPathResult Resolve(std::u16string_view guest_name, std::u16string& out) override {
        if (root_name_.empty()) {
            HostDocumentInfo root;
            if (!host::QueryHostDocument(root_, root) || root.name.empty()) return SharedPathResult::kRootUnavailable;
            root_name_ = Utf8ToUtf16(root.name);
        }
        DocumentNode current = RootNode();
        bool         known   = true;
        return ResolveSharedGuestPath(
            {Win32PathSegment{root_name_, false}}, root_, guest_name,
            [&](const std::string&, const std::u16string& segment) {
                std::optional<DocumentNode> hit;
                if (!known || !FindChild(current, segment, hit).Ok() || !hit) {
                    known = false;
                    return Utf16ToUtf8(segment);
                }
                known = hit->info.directory;
                if (known) current = *hit;
                return hit->info.name;
            },
            out);
    }

    SharedFsStatus MakeDirectory(const std::u16string& path) override {
        return CreateChild(path, true, SharedFsError::kAlreadyExists);
    }

    SharedFsStatus CreateNewFile(const std::u16string& path) override {
        return CreateChild(path, false, SharedFsError::kFileExists);
    }

    SharedFsStatus RemoveEmptyDirectory(const std::u16string& path) override {
        std::vector<DocumentNode> chain;
        SharedFsStatus            st = Locate(path, chain);
        if (!st.Ok()) return st;
        const DocumentNode& target = chain.back();
        if (!target.info.directory) return SharedStatus(SharedFsError::kUnmapped, ENOTDIR);
        std::vector<HostDocumentEntry> children;
        st = Children(target, children);
        if (!st.Ok()) return st;
        if (!children.empty()) return SharedStatus(SharedFsError::kDirNotEmpty, ENOTEMPTY);
        if (!host::DeleteHostDocument(target.uri)) return DocumentError(errno);
        return {};
    }

    SharedFsStatus RemoveFile(const std::u16string& path) override {
        std::vector<DocumentNode> chain;
        const SharedFsStatus      st = Locate(path, chain);
        if (!st.Ok()) return st;
        const DocumentNode& target = chain.back();
        if (target.info.directory || !target.info.writable) return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        if (table_->IsOpen(target.uri)) return SharedStatus(SharedFsError::kSharingViolation, EBUSY);
        if (!host::DeleteHostDocument(target.uri)) return DocumentError(errno);
        return {};
    }

    SharedFsStatus Move(const std::u16string& from, const std::u16string& to) override {
        std::vector<DocumentNode> src_chain;
        SharedFsStatus            st = Locate(from, src_chain);
        if (!st.Ok()) return st;
        if (src_chain.size() < 2u) return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        const DocumentNode& src        = src_chain.back();
        const DocumentNode& src_parent = src_chain[src_chain.size() - 2u];
        if (src.info.directory ? table_->IsOpenAtOrUnder(src.uri, src.path) : table_->IsOpen(src.uri)) {
            return src.info.directory ? SharedStatus(SharedFsError::kAccessDenied, EBUSY)
                                      : SharedStatus(SharedFsError::kSharingViolation, EBUSY);
        }
        std::vector<DocumentNode>      dst_chain;
        std::string                    wanted;
        std::vector<HostDocumentEntry> dst_children;
        st = Parent(to, dst_chain, wanted, dst_children, SharedFsError::kAlreadyExists);
        if (!st.Ok()) return st;
        const HostDocumentEntry* existing     = Match(dst_children, Utf8ToUtf16(wanted));
        const bool               renames_self = existing != nullptr && existing->uri == src.uri;
        if (existing != nullptr && !renames_self) return SharedStatus(SharedFsError::kAlreadyExists, EEXIST);
        for (const DocumentNode& node : dst_chain) {
            if (node.uri == src.uri) return SharedStatus(SharedFsError::kUnmapped, EINVAL);
        }
        return Relocate(src, src_parent, dst_chain.back(), wanted);
    }

    SharedFsStatus SetWriteTime(const std::u16string& path, uint32_t dos_time) override {
        struct timespec times[2];
        SharedFsStatus  st = SharedWriteTimes(dos_time, times);
        if (!st.Ok()) return st;
        std::vector<DocumentNode> chain;
        st = Locate(path, chain);
        if (!st.Ok()) return st;
        const DocumentNode& target = chain.back();
        const int           fd     = host::OpenHostDocument(target.uri, target.info.directory ? "r" : "rw");
        if (fd < 0) return DocumentError(errno);
        const int result = futimens(fd, times);
        const int err    = errno;
        close(fd);
        if (result != 0) return DocumentError(err);
        return {};
    }

    SharedFsStatus SetAttributes(const std::u16string& path, uint8_t attributes) override {
        std::vector<DocumentNode> chain;
        const SharedFsStatus      st = Locate(path, chain);
        if (!st.Ok()) return st;
        constexpr uint8_t kSettable = kSharedReadOnly | kSharedHidden | kSharedSystem | kSharedArchive;
        if ((attributes & kSettable) == (Describe(chain.back().info).attributes & kSettable)) return {};
        return SharedStatus(SharedFsError::kUnmapped, EOPNOTSUPP);
    }

    SharedFsStatus QueryInfo(const std::u16string& path, SharedFsInfo& info) override {
        std::vector<DocumentNode> chain;
        const SharedFsStatus      st = Locate(path, chain);
        if (!st.Ok()) return st;
        info = Describe(chain.back().info);
        return {};
    }

    SharedFsStatus QuerySpace(uint64_t& total, uint64_t& available) override {
        const int fd = host::OpenHostDocument(root_, "r");
        if (fd < 0) return DocumentError(errno);
        struct statvfs sv {};
        const int      result = fstatvfs(fd, &sv);
        const int      err    = errno;
        close(fd);
        if (result != 0) return DocumentError(err);
        SharedSpace(sv, total, available);
        return {};
    }

    SharedFsStatus FindFirst(const std::u16string& pattern, std::unique_ptr<SharedFsFind>& find,
                             SharedFsInfo& info, std::u16string& name) override {
        std::vector<std::string> names;
        bool                     trailing = false;
        if (!Split(pattern, names, trailing)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
        if (trailing || names.empty()) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
        std::vector<DocumentNode> chain;
        SharedFsStatus            st = Walk(names, names.size() - 1u, chain);
        if (!st.Ok()) return st;
        std::vector<HostDocumentEntry> children;
        st = Children(chain.back(), children);
        if (!st.Ok()) return st;
        const std::u16string                 mask = Utf8ToUtf16(names.back());
        std::vector<DocumentSharedFind::Item> items;
        if (!HasWin32Wildcard(mask)) {
            if (const HostDocumentEntry* hit = Match(children, mask)) {
                items.emplace_back(Utf8ToUtf16(hit->info.name), Describe(hit->info));
            }
        } else {
            std::vector<std::string> child_names;
            for (const HostDocumentEntry& child : children) child_names.push_back(child.info.name);
            for (Win32NameMatch& match : MatchWin32Names(child_names, mask)) {
                items.emplace_back(std::move(match.name), Describe(children[match.index].info));
            }
        }
        if (items.empty()) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
        ShareClaim     hold;
        hold.share_read      = true;
        hold.share_write     = true;
        const uint64_t id    = table_->Add({chain.back().uri, chain.back().path, hold});
        auto           found = std::make_unique<DocumentSharedFind>(table_, id, std::move(items));
        st                   = found->Next(info, name);
        find                 = std::move(found);
        return st;
    }

    SharedFsStatus Open(const std::u16string& path, SharedFileAccess access, SharedFileShare share,
                        std::unique_ptr<SharedFsFile>& file) override {
        std::vector<DocumentNode> chain;
        const SharedFsStatus      st = Locate(path, chain);
        if (!st.Ok()) return st;
        const DocumentNode& target = chain.back();
        if (target.info.directory) return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        if (access != SharedFileAccess::kRead && !target.info.writable) {
            return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        }
        const ShareClaim claim = ShareClaimFor(access, share);
        if (table_->Conflicts(target.uri, claim)) return SharedStatus(SharedFsError::kSharingViolation, EAGAIN);
        const char* mode = access == SharedFileAccess::kRead ? "r" : access == SharedFileAccess::kWrite ? "w" : "rw";
        const int   fd   = host::OpenHostDocument(target.uri, mode);
        if (fd < 0) return DocumentError(errno);
        struct stat sb {};
        if (!SetCloseOnExec(fd) || fstat(fd, &sb) != 0 || !S_ISREG(sb.st_mode)) {
            close(fd);
            return SharedStatus(SharedFsError::kAccessDenied, EACCES);
        }
        const uint64_t id = table_->Add({target.uri, target.path, claim});
        file              = std::make_unique<DocumentSharedFile>(fd, table_, id, target.info.writable);
        return {};
    }

private:
    DocumentNode RootNode() const {
        DocumentNode root{root_, root_, {}};
        root.info.directory = true;
        return root;
    }

    bool Split(const std::u16string& host_path, std::vector<std::string>& names, bool& trailing) const {
        const std::string path = Utf16ToUtf8(host_path);
        if (path.compare(0, root_.size(), root_) != 0) return false;
        if (path.size() > root_.size() && path[root_.size()] != '/') return false;
        trailing = path.size() > root_.size() && path.back() == '/';
        names    = PathSegments(path, root_.size(), kPosixPathSeparators<char>);
        return true;
    }

    SharedFsStatus Children(const DocumentNode& dir, std::vector<HostDocumentEntry>& children) const {
        children.clear();
        if (host::ListHostDocumentChildren(dir.uri, children)) return {};
        return errno == ENOENT ? SharedStatus(SharedFsError::kPathNotFound, ENOENT) : DocumentError(errno);
    }

    SharedFsStatus FindChild(const DocumentNode& dir, std::u16string_view name,
                             std::optional<DocumentNode>& child) const {
        std::vector<HostDocumentEntry> children;
        const SharedFsStatus           st = Children(dir, children);
        if (!st.Ok()) return st;
        if (const HostDocumentEntry* hit = Match(children, name)) child = ChildNode(dir, *hit);
        return {};
    }

    SharedFsStatus Walk(const std::vector<std::string>& names, size_t count, std::vector<DocumentNode>& chain) const {
        chain.assign(1, RootNode());
        for (size_t i = 0; i < count; ++i) {
            std::optional<DocumentNode> hit;
            const SharedFsStatus        st = FindChild(chain.back(), Utf8ToUtf16(names[i]), hit);
            if (!st.Ok()) return st;
            if (!hit) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
            if (!hit->info.directory) return SharedStatus(SharedFsError::kPathNotFound, ENOTDIR);
            chain.push_back(std::move(*hit));
        }
        return {};
    }

    SharedFsStatus Locate(const std::u16string& path, std::vector<DocumentNode>& chain) const {
        std::vector<std::string> names;
        bool                     trailing = false;
        if (!Split(path, names, trailing)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
        if (names.empty()) {
            chain.assign(1, RootNode());
            if (!host::QueryHostDocument(root_, chain.back().info)) {
                return errno == ENOENT ? SharedStatus(SharedFsError::kPathNotFound, ENOENT) : DocumentError(errno);
            }
            return {};
        }
        SharedFsStatus st = Walk(names, names.size() - 1u, chain);
        if (!st.Ok()) return st;
        std::optional<DocumentNode> hit;
        st = FindChild(chain.back(), Utf8ToUtf16(names.back()), hit);
        if (!st.Ok()) return st;
        if (!hit) return SharedStatus(SharedFsError::kFileNotFound, ENOENT);
        chain.push_back(std::move(*hit));
        return {};
    }

    SharedFsStatus Parent(const std::u16string& path, std::vector<DocumentNode>& chain, std::string& name,
                          std::vector<HostDocumentEntry>& children, SharedFsError root_error) const {
        std::vector<std::string> names;
        bool                     trailing = false;
        if (!Split(path, names, trailing)) return SharedStatus(SharedFsError::kPathNotFound, ENOENT);
        if (names.empty()) return SharedStatus(root_error, EEXIST);
        SharedFsStatus st = Walk(names, names.size() - 1u, chain);
        if (!st.Ok()) return st;
        name = names.back();
        if (!IsWin32FileName(Utf8ToUtf16(name))) return SharedStatus(SharedFsError::kInvalidName, EINVAL);
        return Children(chain.back(), children);
    }

    SharedFsStatus CreateChild(const std::u16string& path, bool directory, SharedFsError exists) {
        std::vector<DocumentNode>      chain;
        std::string                    name;
        std::vector<HostDocumentEntry> children;
        const SharedFsStatus           st = Parent(path, chain, name, children, exists);
        if (!st.Ok()) return st;
        if (Match(children, Utf8ToUtf16(name)) != nullptr) return SharedStatus(exists, EEXIST);
        std::string created;
        if (!host::CreateHostDocument(chain.back().uri, directory, name, created)) return DocumentError(errno);
        HostDocumentInfo made;
        const bool       queried = host::QueryHostDocument(created, made);
        const int        err     = errno;
        if (queried && made.name == name) return {};
        if (!host::DeleteHostDocument(created)) return DocumentError(errno);
        if (!queried) return DocumentError(err);
        return Children(chain.back(), children).Ok() && Match(children, Utf8ToUtf16(name)) != nullptr
                   ? SharedStatus(exists, EEXIST)
                   : SharedStatus(SharedFsError::kInvalidName, EINVAL);
    }

    bool Rename(std::string& uri, std::string& name, const std::string& wanted) const {
        std::string renamed;
        if (!host::RenameHostDocument(uri, wanted, renamed)) return false;
        HostDocumentInfo info;
        if (!host::QueryHostDocument(renamed, info)) return false;
        uri  = renamed;
        name = info.name;
        return true;
    }

    SharedFsStatus Relocate(const DocumentNode& src, const DocumentNode& src_parent, const DocumentNode& dst_parent,
                            const std::string& wanted) {
        const bool moving = dst_parent.uri != src_parent.uri;
        if (!moving && src.info.name == wanted) return {};
        std::string uri  = src.uri;
        std::string name = src.info.name;
        if (!Rename(uri, name, name + kMoveSuffix)) return DocumentError(errno);
        if (moving) {
            std::string moved;
            if (!host::MoveHostDocument(uri, src_parent.uri, dst_parent.uri, moved)) {
                const SharedFsStatus failure = DocumentError(errno);
                return Restore(uri, name, false, src, src_parent, dst_parent, failure);
            }
            uri = moved;
        }
        if (!Rename(uri, name, wanted)) {
            const SharedFsStatus failure = DocumentError(errno);
            return Restore(uri, name, moving, src, src_parent, dst_parent, failure);
        }
        if (name == wanted) return {};
        LOG(GuestAdditions, "[FolderShare] document rename to '%s' produced '%s'; restoring '%s'\n", wanted.c_str(),
            name.c_str(), src.info.name.c_str());
        return Restore(uri, name, moving, src, src_parent, dst_parent, SharedStatus(SharedFsError::kInvalidName, EINVAL));
    }

    SharedFsStatus Restore(std::string uri, std::string name, bool moved, const DocumentNode& src,
                           const DocumentNode& src_parent, const DocumentNode& dst_parent,
                           const SharedFsStatus& failure) const {
        if (moved) {
            std::string back;
            if (!host::MoveHostDocument(uri, dst_parent.uri, src_parent.uri, back)) return DocumentError(errno);
            uri = back;
        }
        if (name != src.info.name && !Rename(uri, name, src.info.name)) return DocumentError(errno);
        return failure;
    }

    std::string                        root_;
    std::u16string                     root_name_;
    std::shared_ptr<DocumentOpenTable> table_;
};

}

std::unique_ptr<HostSharedFolder> OpenDocumentSharedFolder(const std::string& root) {
    return std::make_unique<DocumentSharedFolder>(root);
}

}

}
