#include "msm8255_i2c_slaves.h"

#include "../i2c_slave.h"

namespace cerfcore {

uint32_t Msm8255I2cSlaves::SlaveIndex(uint32_t bus, uint8_t addr) {
    const auto slaves = Slaves(bus);
    for (uint32_t i = 0; i < slaves.size(); ++i) {
        if (slaves[i]->MatchesAddress(addr)) return i;
    }
    return kNoSlave;
}

bool Msm8255I2cSlaves::OwnedBySlave(uint32_t bus, uint8_t addr) {
    for (I2cSlave* slave : Slaves(bus)) {
        if (slave->OwnsAddress(addr)) return true;
    }
    return false;
}

void Msm8255I2cSlaves::SaveBusState(uint32_t bus, StateWriter& w) {
    for (I2cSlave* slave : Slaves(bus)) slave->SaveState(w);
}

void Msm8255I2cSlaves::RestoreBusState(uint32_t bus, StateReader& r) {
    for (I2cSlave* slave : Slaves(bus)) slave->RestoreState(r);
}

void Msm8255I2cSlaves::PostRestoreBus(uint32_t bus) {
    for (I2cSlave* slave : Slaves(bus)) slave->PostRestore();
}

}
