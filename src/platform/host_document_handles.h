#pragma once

#include "host_fd_shared_file.h"
#include "host_share_modes.h"
#include "host_shared_folder.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace cerfcore {

namespace host_platform {

class DocumentOpenTable {
public:
    struct Entry {
        std::string uri;
        std::string path;
        ShareClaim  claim;
    };

    uint64_t Add(Entry entry);
    void     Remove(uint64_t id);

    bool Conflicts(const std::string& uri, const ShareClaim& claim) const;
    bool IsOpen(const std::string& uri) const;
    bool IsOpenAtOrUnder(const std::string& uri, const std::string& path) const;

private:
    std::unordered_map<uint64_t, Entry> entries_;
    uint64_t                            next_id_ = 0;
};

class DocumentSharedFind : public SharedFsFind {
public:
    using Item = std::pair<std::u16string, SharedFsInfo>;

    DocumentSharedFind(std::shared_ptr<DocumentOpenTable> table, uint64_t id, std::vector<Item> items);
    ~DocumentSharedFind() override;
    DocumentSharedFind(const DocumentSharedFind&)            = delete;
    DocumentSharedFind& operator=(const DocumentSharedFind&) = delete;

    SharedFsStatus Next(SharedFsInfo& info, std::u16string& name) override;

private:
    std::shared_ptr<DocumentOpenTable> table_;
    uint64_t                           id_;
    std::vector<Item>                  items_;
    size_t                             next_ = 0;
};

class DocumentSharedFile : public FdSharedFile {
public:
    DocumentSharedFile(int fd, std::shared_ptr<DocumentOpenTable> table, uint64_t id, bool writable);
    ~DocumentSharedFile() override;

    SharedFsStatus Info(SharedFsInfo& info) override;

private:
    std::shared_ptr<DocumentOpenTable> table_;
    uint64_t                           id_;
    bool                               writable_;
};

}

}
