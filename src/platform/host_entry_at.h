#pragma once

#include "host_entry_stat.h"

namespace cerfcore {

namespace host_platform {

bool ReadEntryAt(int dirfd, const char* path, int flags, EntryStat& out);

}

}
