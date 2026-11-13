#pragma once

#include <cerfcore/core/service.h>
#include "../../peripherals/serial/serial_16550.h"
#include "../../peripherals/serial/serial_device_clock_timer.h"

#include <cstdint>
#include <memory>

namespace cerfcore {

class StateWriter;
class StateReader;

class CasioJx670Modem : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    bool TryReadByte (uint32_t off, uint8_t&  out);
    bool TryWriteByte(uint32_t off, uint8_t   value);
    bool TryReadHalf (uint32_t off, uint16_t& out);
    bool TryWriteHalf(uint32_t off, uint16_t  value);
    bool TryReadWord (uint32_t off, uint32_t& out);
    bool TryWriteWord(uint32_t off, uint32_t  value);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);
    void PostRestore();

private:
    void OnUartIrq(bool asserted);

    static constexpr uint32_t k16550Base = 0x8680u;   /* regs 0..7 stride 4 (serial.dll dword_F58130 [1..8]) */
    bool In16550(uint32_t off) const {
        return off >= k16550Base && off < k16550Base + 8u * 4u &&
               ((off - k16550Base) & 3u) == 0u;
    }

    SerialDeviceClockTimer       line_timer_{emu_, [this] { uart_->Port().OnTimer(); }};
    std::unique_ptr<Serial16550> uart_;
    uint8_t  ctrl_8600_   = 0;   /* serial.dll dword_F58130[0]=0x600 -> companion 0x8600 */
    uint16_t socket_a1c0_ = 0;
    uint16_t socket_a1c4_ = 0;
};

}
