#pragma once

#include "../../peripherals/usb/ulpi_phy.h"
#include "msm8255_usb_port.h"

#include <cstdint>

namespace cerfcore {

class Msm8255UsbPhy : public UlpiPhy {
public:
    void PowerOn();
    bool SetAttachment(Msm8255UsbAttachment attachment);

    uint8_t LineState() const override;

private:
    bool ReadVendor(uint8_t addr, uint8_t& value) const override;
    bool WriteVendor(uint8_t addr, uint8_t value) override;

    bool DetectorEnabled() const;
    bool ContactSourceOn() const;

    Msm8255UsbAttachment attachment_ = Msm8255UsbAttachment::kNone;
};

}
