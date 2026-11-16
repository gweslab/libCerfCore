#include "imx6_i2c_bus.h"

#include "../i2c_slave.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "imx6_id.h"

namespace cerfcore {

REGISTER_SERVICE(Imx6I2cBus);

bool Imx6I2cBus::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

void Imx6I2cBus::Register(I2cSlave* device, uint32_t controller_base) {
    devices_.push_back({device, controller_base});
}

I2cSlave* Imx6I2cBus::Find(uint32_t controller_base, uint8_t slave_addr) const {
    for (const Attachment& a : devices_) {
        if (a.controller_base == controller_base && a.device->MatchesAddress(slave_addr)) return a.device;
    }
    return nullptr;
}

void Imx6I2cBus::SaveDevices(uint32_t controller_base, StateWriter& w) const {
    for (const Attachment& a : devices_) {
        if (a.controller_base == controller_base) a.device->SaveState(w);
    }
}

void Imx6I2cBus::RestoreDevices(uint32_t controller_base, StateReader& r) const {
    for (const Attachment& a : devices_) {
        if (a.controller_base == controller_base) a.device->RestoreState(r);
    }
}

}
