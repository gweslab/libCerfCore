#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Usb3317UlpiPhy {
public:
    void PowerOn();
    bool Read(uint8_t addr, uint8_t& value) const;
    bool Write(uint8_t addr, uint8_t value);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint8_t kRegisterCount = 0x40u;

    uint8_t regs_[kRegisterCount] = {};
};

}
