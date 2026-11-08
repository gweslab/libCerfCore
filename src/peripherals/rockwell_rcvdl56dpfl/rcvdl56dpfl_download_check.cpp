#include "rcvdl56dpfl_download_check.h"

namespace cerfcore {

namespace {

struct KnownBlock {
    uint8_t  mode;
    uint16_t address;
    uint32_t length;
    uint32_t crc32;
    uint8_t  check;
};

constexpr KnownBlock kKnownBlocks[] = {
    {79u, 0x1000u, 1094u, 0x191C2BB8u, 0x46u},
    {79u, 0x1000u, 1094u, 0x385E6D21u, 0x77u},
    {79u, 0x1000u, 1094u, 0xF9BEDBB4u, 0x38u},
    {79u, 0x1450u, 2957u, 0x86B80D63u, 0xBCu},
    {79u, 0x1450u, 4963u, 0x4392A77Au, 0xC4u},
    {79u, 0x1450u, 4983u, 0xD4D456E2u, 0xE9u},
    {79u, 0x27B3u, 1890u, 0xF352838Eu, 0x59u},
    {79u, 0x27B3u, 4759u, 0xE58261D5u, 0x5Fu},
    {79u, 0x27C7u, 1893u, 0x31110888u, 0xF1u},
    {79u, 0x27C7u, 4741u, 0xE6F8A436u, 0x8Cu},
    {79u, 0x2E00u, 4436u, 0x0919F054u, 0x87u},
    {79u, 0x2F20u, 1807u, 0x441BF17Au, 0x07u},
    {79u, 0x2F30u, 1839u, 0xE49A2908u, 0xB8u},
    {79u, 0x3A80u, 1092u, 0x3017DA40u, 0x49u},
    {79u, 0x3A80u, 1092u, 0x6E9DDE78u, 0x70u},
    {79u, 0x3F00u, 142u, 0x3D479E5Au, 0xE2u},
    {79u, 0x3F00u, 149u, 0x490CC3C2u, 0xE2u},
    {79u, 0x3FC0u, 58u, 0xE0EB38A0u, 0xC9u},
    {95u, 0x7001u, 736u, 0x1628BA6Bu, 0x8Bu},
    {95u, 0x7001u, 736u, 0x496122FCu, 0xB1u},
    {95u, 0x7001u, 736u, 0xC6D411E0u, 0xC1u},
    {95u, 0x70B9u, 944u, 0x04651367u, 0x79u},
    {95u, 0x70B9u, 944u, 0xE01734B9u, 0xA5u},
    {95u, 0x7100u, 7812u, 0x64FBF483u, 0xF6u},
    {95u, 0x71A5u, 9388u, 0x2BE495A6u, 0xF5u},
    {95u, 0x71A5u, 9528u, 0xDB82DC45u, 0x59u},
    {95u, 0x7900u, 2576u, 0x2B1C4C21u, 0xCFu},
    {95u, 0x7AF6u, 1060u, 0x03820577u, 0x4Cu},
    {95u, 0x7AFAu, 1044u, 0x0BAC7F30u, 0x71u},
};

}

std::optional<uint8_t> Rcvdl56dpflDownloadCheck(const Rcvdl56dpflDownloadBlock& block) {
    for (const KnownBlock& k : kKnownBlocks)
        if (k.mode == block.mode && k.address == block.address && k.length == block.length && k.crc32 == block.crc32)
            return k.check;
    return std::nullopt;
}

}
