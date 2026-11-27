#pragma once

#include "../../socs/i2c_register_pointer.h"
#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class StateWriter;
class StateReader;

class TiBq27520RomMode : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool Active() const { return active_; }
    void Enter();

    void    TxnStart();
    void    WriteByte(uint8_t byte);
    uint8_t ReadByte();

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    static constexpr uint8_t kStatusRegister = 0x66u;

    void     Execute();
    void     VerifyChecksum(uint8_t command, uint16_t operand_sum) const;
    uint16_t SumRegisters(uint8_t first, uint32_t count) const;
    void     SetResult(uint8_t reg, uint8_t value);

    bool                                   active_          = false;
    I2cRegisterPointer                     pointer_;
    std::array<uint8_t, kStatusRegister>   regs_{};
    std::array<uint8_t, kStatusRegister>   written_{};
    bool                                   status_valid_    = false;
    uint8_t                                status_          = 0u;
};

}
