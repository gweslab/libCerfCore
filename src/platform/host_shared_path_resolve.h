#pragma once

#include "host_shared_folder.h"
#include "host_win32_name_rules.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cerfcore {

namespace host_platform {

using SharedNameLookup = std::function<std::string(const std::string& dir, const std::u16string& name)>;

SharedPathResult ResolveSharedGuestPath(const std::vector<Win32PathSegment>& root, std::string host_root,
                                        std::u16string_view guest_name, const SharedNameLookup& lookup,
                                        std::u16string& out);

}

}
