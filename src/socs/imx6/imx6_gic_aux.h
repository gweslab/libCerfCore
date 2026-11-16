#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

namespace imx6_gic_detail {

class Imx6GicAux {
public:
    bool ScuRead(uint32_t off, uint32_t aux_control_register, uint32_t& value) const;
    bool ScuWrite(uint32_t off, uint32_t value, const char*& unmodelled);

    void Reset();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    uint32_t scu_control_ = 0;
    uint32_t scu_access_control_ = 0x0000000Fu;
};

}

}
