#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class AdrenoA2xxInterrupts : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool     Owns(uint32_t idx) const;
    uint32_t Read(uint32_t idx) const;
    void     Write(uint32_t idx, uint32_t value, uint32_t address);
    void     RaiseCp(uint32_t bits, uint32_t address);
    bool     Asserted() const;

    void PowerOnReset();
    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    struct Block {
        uint32_t mask_idx;
        uint32_t status_idx;
        uint32_t ack_idx;
        uint32_t defined;
        uint32_t master_bit;
    };

    static constexpr uint32_t kBlockCount = 4u;

    static const Block kBlocks[kBlockCount];

    uint32_t MasterSignal() const;

    uint32_t status_[kBlockCount] = {};
    uint32_t mask_[kBlockCount]   = {};
};

}
