#pragma once

#include "board_database.h"

#include <string_view>

namespace cerfcore {

const char* CpuArchName(CpuArch arch);
CpuArch     CpuArchFromName(std::string_view name);

}
