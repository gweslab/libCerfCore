#include "i2c_register_pointer.h"

namespace cerfcore {

void I2cRegisterPointer::Start() { pending_ = true; }

bool I2cRegisterPointer::Select(uint8_t byte) {
    if (!pending_) return false;
    pending_  = false;
    reg_      = byte;
    selected_ = true;
    return true;
}

void I2cRegisterPointer::EndSelect() { pending_ = false; }

uint16_t I2cRegisterPointer::Advance() { return reg_++; }

void I2cRegisterPointer::Point(uint16_t reg) {
    reg_      = reg;
    selected_ = true;
}

void I2cRegisterPointer::Clear() { selected_ = false; }

void I2cRegisterPointer::Reset(uint16_t reg, bool selected) { Restore(reg, selected, false); }

void I2cRegisterPointer::Restore(uint16_t reg, bool selected, bool pending) {
    reg_      = reg;
    selected_ = selected;
    pending_  = pending;
}

bool I2cRegisterPointer::Selected() const { return selected_; }

bool I2cRegisterPointer::Pending() const { return pending_; }

uint16_t I2cRegisterPointer::Register() const { return reg_; }

}
