#include <cerfcore/core/instance_threads.h>

#include <cerfcore/core/cerf_emulator.h>

namespace cerfcore {

REGISTER_SERVICE(InstanceThreads);

void InstanceThreads::OnReady() { fatal_ = &emu_.Get<Fatal>(); }

}
