#include <cerfcore/core/cpu_arch_name.h>

#include "name_table.h"

#include <cstddef>

namespace cerfcore {

namespace {

constexpr std::pair<std::string_view, CpuArch> kCpuArchNames[] = {
    {"ARM", CpuArch::kArm},
    {"MIPS", CpuArch::kMips},
};

}

const char* CpuArchName(CpuArch arch) { return kCpuArchNames[static_cast<size_t>(arch)].first.data(); }

CpuArch CpuArchFromName(std::string_view name) { return ValueByName(kCpuArchNames, name); }

}
