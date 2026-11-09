#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/cpu/emulated_memory.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Sa1110SdramDecode : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void ApplyMdcnfg(uint32_t mdcnfg);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r) const;

private:
    static EmulatedMemory::AddressDecode Build(uint32_t rows, uint32_t columns,
                                               uint32_t drac, uint32_t native_drac);

    uint32_t rows_    = 0;
    uint32_t columns_ = 0;
    uint32_t banks_   = 0;
    std::array<EmulatedMemory::AddressDecode, 7> decodes_{};
    const EmulatedMemory::AddressDecode* active_ = nullptr;
};

}
