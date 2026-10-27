#include "host_shared_path_resolve.h"

#include "host_shared_entry.h"

#include <cerfcore/core/utf16.h>

namespace cerfcore {

namespace host_platform {

SharedPathResult ResolveSharedGuestPath(const std::vector<Win32PathSegment>& root, std::string host_root,
                                        std::u16string_view guest_name, const SharedNameLookup& lookup,
                                        std::u16string& out) {
    const Win32FullPath full = NormalizeWin32Path(root, guest_name);
    if (full.segments.size() < root.size()) return SharedPathResult::kInvalidName;
    for (size_t i = 0; i < root.size(); ++i) {
        if (!SameWin32Name(full.segments[i].text, root[i].text)) return SharedPathResult::kInvalidName;
    }
    std::string path = std::move(host_root);
    for (size_t i = root.size(); i < full.segments.size(); ++i) {
        const bool literal = i + 1u == full.segments.size() && !full.trailing_separator;
        path = JoinSharedPath(path, literal ? Utf16ToUtf8(full.segments[i].text) : lookup(path, full.segments[i].text));
    }
    if (full.trailing_separator) path += "/";
    out = Utf8ToUtf16(path);
    return SharedPathResult::kResolved;
}

}

}
