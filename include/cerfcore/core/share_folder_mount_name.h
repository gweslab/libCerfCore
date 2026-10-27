#pragma once

#include <cstddef>
#include <string>

namespace cerfcore {

constexpr size_t kShareFolderMountNameMaxWchars      = 63;
constexpr char   kShareFolderMountNameInvalidChars[] = "\\/:*?\"<>|";

bool IsValidShareFolderMountName(const std::string& utf8);

}
