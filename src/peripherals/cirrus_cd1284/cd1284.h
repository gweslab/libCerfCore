#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "../external_device_clock.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class Cd1284 : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    uint32_t MmioSize() const override { return kRegisterCount << RegisterShift(); }

    uint8_t ReadByte(uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    static constexpr uint32_t kRegisterCount = 0x80u;
    static constexpr uint32_t kSerialCount   = 2u;

protected:
    virtual uint32_t RegisterShift() const       = 0;
    virtual uint8_t  GpioInputLevels() const     = 0;
    virtual uint8_t  HostHandshakeLevels() const = 0;

private:
    struct SerialChannel {
        uint8_t srer = 0u;
        uint8_t cor1 = 0u;
        uint8_t cor2 = 0u;
        uint8_t cor3 = 0u;
        uint8_t cor4 = 0u;
        uint8_t cor5 = 0u;
        uint8_t rtpr = 0u;
        uint8_t tbpr = 0x41u;
        uint8_t tcor = 0x01u;
        uint8_t rbpr = 0x41u;
        uint8_t rcor = 0x01u;
        bool    tx_enabled = false;
        bool    rx_enabled = false;
        std::string tx_line;
    };

    void PowerOnReset();
    bool Resetting();
    void ChannelCommand(uint32_t addr, uint8_t value);
    bool TransmitRequest(uint32_t serial) const;
    int  RequestingTransmitter() const;
    void    WriteSerial(uint32_t addr, uint32_t index, uint8_t value);
    void    WriteParallel(uint32_t addr, uint32_t index, uint8_t value);
    SerialChannel& Selected(uint32_t addr, const char* op, uint8_t value);

    SerialChannel serial_[kSerialCount];
    ExternalDeviceClock* clock_ = nullptr;
    ExternalDeviceClock::Deadline reset_;
    uint8_t car_           = 0u;
    uint8_t gfrcr_         = 0u;
    uint8_t gpdir_         = 0u;
    uint8_t gpio_          = 0u;
    uint8_t ppr_           = 0u;
    uint8_t pcr_           = 0u;
    uint8_t ovr_           = 0u;
    uint8_t pcrr_          = 0u;
    int     tx_service_    = -1;
};

}
