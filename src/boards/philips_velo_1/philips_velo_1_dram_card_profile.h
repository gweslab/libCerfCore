#pragma once

#include <cstdint>

namespace cerfcore {

struct PhilipsVelo1DramCardProfile {
    uint32_t megabytes;
    uint8_t  row_bits;
    uint8_t  column_bits;
};

/* (row, column) address-bit pairs are entries of asc_9F40A904 @0x9F40A904, the table
   philips_velo_1_ce1 nk.exe sub_9F42448C @0x9F42448C matches the card's ID EEPROM
   against. */
inline constexpr PhilipsVelo1DramCardProfile kPhilipsVelo1DramCards[] = {
    {  1u, 10u,  9u },
    {  2u, 12u,  8u },
    {  4u, 11u, 10u },
    {  8u, 12u, 10u },
    { 16u, 12u, 11u },
};

}
