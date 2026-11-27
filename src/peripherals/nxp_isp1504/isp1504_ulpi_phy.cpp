#include "isp1504_ulpi_phy.h"

namespace cerfcore {

namespace {

constexpr uint8_t kId[4] = {0xCCu, 0x04u, 0x04u, 0x15u};

}

void Isp1504UlpiPhy::PowerOn() {
    PowerOnStandard(kId);
}

bool Isp1504UlpiPhy::ReadVendor(uint8_t, uint8_t&) const { return false; }

bool Isp1504UlpiPhy::WriteVendor(uint8_t, uint8_t) { return false; }

}
