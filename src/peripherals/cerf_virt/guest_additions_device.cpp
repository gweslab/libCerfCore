#include "guest_additions_device.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/device_config.h>
#include <cerfcore/host/machine_object_tree.h>

namespace cerfcore {

REGISTER_SERVICE(GuestAdditionsDevice);

bool GuestAdditionsDevice::ShouldRegister() { return emu_.Get<DeviceConfig>().guest_additions; }

void GuestAdditionsDevice::OnReady() { emu_.Get<MachineObjectTree>().Add(this); }

void GuestAdditionsDevice::LiveResizeChanged() { Changed(); }

}
