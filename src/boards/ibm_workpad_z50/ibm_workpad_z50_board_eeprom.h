#pragma once

#include "../../peripherals/eeprom_24cxx/eeprom_24cxx.h"

#include <cstdint>

namespace cerfcore {

class IbmWorkpadZ50BoardEeprom : public Eeprom24cxx {
public:
    using Eeprom24cxx::Eeprom24cxx;

    bool ShouldRegister() override;

protected:
    void        FillCells() override;
    uint8_t     DeviceAddress() const override;
    uint16_t    PageBytes() const override;
    const char* PartName() const override;
};

}
