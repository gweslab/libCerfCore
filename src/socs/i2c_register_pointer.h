#pragma once

#include <cstdint>

namespace cerfcore {

class I2cRegisterPointer {
public:
    void     Start();
    bool     Select(uint8_t byte);
    void     EndSelect();
    uint16_t Advance();
    void     Point(uint16_t reg);
    void     Clear();
    void     Reset(uint16_t reg, bool selected);
    void     Restore(uint16_t reg, bool selected, bool pending);

    bool     Selected() const;
    bool     Pending() const;
    uint16_t Register() const;

private:
    uint16_t reg_      = 0u;
    bool     selected_ = false;
    bool     pending_  = false;
};

}
