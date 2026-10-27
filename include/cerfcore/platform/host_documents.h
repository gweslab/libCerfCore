#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cerfcore {

struct HostDocumentInfo {
    std::string name;
    bool        directory   = false;
    bool        writable    = false;
    uint64_t    size        = 0;
    int64_t     modified_ms = 0;
};

struct HostDocumentEntry {
    std::string      uri;
    HostDocumentInfo info;
};

namespace host {

int  OpenHostDocument(const std::string& uri, const char* mode);
bool QueryHostDocument(const std::string& uri, HostDocumentInfo& info);
bool ListHostDocumentChildren(const std::string& uri, std::vector<HostDocumentEntry>& children);
bool CreateHostDocument(const std::string& parent_uri, bool directory, const std::string& name,
                        std::string& created_uri);
bool DeleteHostDocument(const std::string& uri);
bool RenameHostDocument(const std::string& uri, const std::string& name, std::string& renamed_uri);
bool MoveHostDocument(const std::string& uri, const std::string& parent_uri, const std::string& target_parent_uri,
                      std::string& moved_uri);

}

}
