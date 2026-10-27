#pragma once
#include <string>

namespace cerfcore {

struct DeviceMeta;

bool        OsNameHasVersion(const std::string& name, int major, int minor);
std::string OsDisplayLabel(const DeviceMeta& meta);

}
