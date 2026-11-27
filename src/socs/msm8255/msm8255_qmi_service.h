#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

struct Msm8255QmiAnswer {
    uint16_t             error = 0u;
    std::vector<uint8_t> tlvs;
};

class Msm8255QmiService : public Service {
public:
    using Service::Service;

    virtual uint8_t QmiService() const = 0;

    virtual Msm8255QmiAnswer Answer(uint32_t port, uint8_t client, uint16_t message,
                                    const std::vector<uint8_t>& tlvs) = 0;

    virtual void SaveState(StateWriter& w)   = 0;
    virtual void RestoreState(StateReader& r) = 0;
};

}
