#pragma once

#include <cstdint>
#include <vector>

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

class I2cSlave;

class Imx6I2cBus : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void Register(I2cSlave* device, uint32_t controller_base);
    I2cSlave* Find(uint32_t controller_base, uint8_t slave_addr) const;

    void SaveDevices(uint32_t controller_base, StateWriter& w) const;
    void RestoreDevices(uint32_t controller_base, StateReader& r) const;

private:
    struct Attachment {
        I2cSlave* device;
        uint32_t controller_base;
    };

    std::vector<Attachment> devices_;
};

}
