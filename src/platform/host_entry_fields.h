#pragma once

#include "host_entry_stat.h"

namespace cerfcore {

namespace host_platform {

void FillEntryFields(EntryStat& out, unsigned mode, uint64_t size, int64_t mtime, int64_t btime);

}

}
