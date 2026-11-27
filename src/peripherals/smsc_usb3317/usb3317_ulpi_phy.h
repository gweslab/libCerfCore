#pragma once

#include "../usb/ulpi_phy.h"

#include <cstdint>

namespace cerfcore {

class Usb3317UlpiPhy : public UlpiPhy {
public:
    void PowerOn();

private:
    bool ReadVendor(uint8_t addr, uint8_t& value) const override;
    bool WriteVendor(uint8_t addr, uint8_t value) override;
};

}
