#include <cerfcore/core/instance_gate.h>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

namespace cerfcore {

bool InstanceGate::RunBody(void (*call)(void* body, CerfEmulator& emu), void* body) const {
    if (!emu_) return false;
    CerfEmulator& emu = *emu_;
    return emu.Get<Fatal>().Guard([&] { call(body, emu); });
}

}
