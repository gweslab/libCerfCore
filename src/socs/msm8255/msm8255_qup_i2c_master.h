#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class I2cSlave;
class StateReader;
class StateWriter;

class Msm8255QupI2cMaster : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool Execute(uint16_t item, std::vector<uint8_t>& received);

    bool BusOpen() const { return open_; }
    void Release();
    void Reset();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    bool      Start(uint8_t addr_byte);
    bool      Transmit(uint8_t byte);
    void      Receive(uint32_t count, std::vector<uint8_t>& received);
    I2cSlave& Slave();

    bool     open_        = false;
    bool     reading_     = false;
    uint32_t addr_        = 0u;
    uint32_t slave_index_ = 0u;
};

}
