#pragma once

#include <cerfcore/core/service.h>
#include "../../socs/uart_endpoint.h"

#include <cstddef>
#include <cstdint>

namespace cerfcore {

class Imx51Gpio1;
class Imx51Uart1;

/* The SYNC2 Bluetooth controller (Broadcom BCM4325) on UART1 = COM1. Answers
   the guest BTHUART patchram handshake; without a reply the head unit reboots
   (bthuart.dll InitBTHardware sub_C0775DE0: 3 failed BTPreLoad attempts ->
   RebootHandler). */
class FordSyncGen2BtHci : public Service, public UartEndpoint {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    /* UartEndpoint: a guest UART1 TX byte (JIT thread). Reassembles the H4 HCI
       command packet and injects the matching Command Complete event. */
    void OnGuestTx(uint8_t byte) override;

    /* Forwarded from Imx51Uart1's Save/Restore: the mid-packet command
       accumulator (a hibernation mid-handshake resumes consistently). */
    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

private:
    void Reply();
    void PowerOn();
    void OnResetLine();

    static constexpr uint8_t kFactoryBdAddr[6] = {0x32u, 0x59u, 0x53u, 0x00u, 0xCEu, 0x02u};

    Imx51Uart1* uart_       = nullptr;
    Imx51Gpio1* gpio1_      = nullptr;
    bool        reset_held_ = false;

    /* H4 command accumulator: 0x01 <op_lo> <op_hi> <plen> [plen params]. The
       4-byte header plus a single-byte length (max 255) bound a packet to 259
       bytes; the assembler can never index past that. */
    static constexpr std::size_t kMaxCmd = 259u;
    uint8_t     cmd_[kMaxCmd] = {0};
    uint32_t    n_            = 0;   /* bytes assembled in cmd_ so far */
    uint32_t    patchram_cmds_ = 0; /* log throttle for the WriteRAM stream */

    uint8_t     bd_addr_[6] = {kFactoryBdAddr[0], kFactoryBdAddr[1], kFactoryBdAddr[2],
                               kFactoryBdAddr[3], kFactoryBdAddr[4], kFactoryBdAddr[5]};
};

}
