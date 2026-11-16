#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class MiiPhy : public Service {
public:
    using Service::Service;

    virtual uint16_t ReadRegister(uint32_t phy, uint32_t reg, bool link_up) const = 0;
    virtual void WriteRegister(uint32_t phy, uint32_t reg, uint16_t value) = 0;
    virtual void ResetPhyState() = 0;
    virtual void SavePhyState(StateWriter& writer) const = 0;
    virtual void RestorePhyState(StateReader& reader) = 0;
};

}
