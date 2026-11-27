#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <span>

namespace cerfcore {

class I2cSlave;
class StateReader;
class StateWriter;

inline constexpr uint32_t kMsm8255I2c1Bus = 0u;
inline constexpr uint32_t kMsm8255I2c2Bus = 1u;
inline constexpr uint32_t kMsm8255QupBus  = 2u;

class Msm8255I2cSlaves : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kNoSlave = 0xFFFFFFFFu;

    virtual std::span<I2cSlave* const> Slaves(uint32_t bus) = 0;

    uint32_t SlaveIndex(uint32_t bus, uint8_t addr);
    bool     OwnedBySlave(uint32_t bus, uint8_t addr);

    void SaveBusState(uint32_t bus, StateWriter& w);
    void RestoreBusState(uint32_t bus, StateReader& r);
    void PostRestoreBus(uint32_t bus);
};

}
