#include "compaq_ipaq_h3000_battery_curve.h"

#include "compaq_ipaq_h3000_family_id.h"
#include "../../socs/sa11xx/sa1110_sdram_geometry.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>

#include <algorithm>

namespace cerfcore {

namespace {

constexpr std::array<uint16_t, 10> kSmallDramTenthVoltages = {840, 849, 856, 863, 869,
                                                              878, 889, 900, 914, 922};
constexpr std::array<uint16_t, 10> kLargeDramTenthVoltages = {849, 856, 858, 865, 873,
                                                              882, 892, 902, 914, 922};

constexpr uint64_t kSdramBusBytes     = 4;
constexpr uint64_t kLargeDramBytes    = 64ull << 20;

}

REGISTER_SERVICE(CompaqIpaqH3000BatteryCurve);

bool CompaqIpaqH3000BatteryCurve::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

void CompaqIpaqH3000BatteryCurve::OnReady() {
    const auto&    geometry = emu_.Get<Sa1110SdramGeometry>();
    const uint64_t dram     = kSdramBusBytes
                              << (geometry.RowBits() + geometry.ColumnBits() + geometry.BankBits());
    tenth_voltages_ = dram == kLargeDramBytes ? &kLargeDramTenthVoltages : &kSmallDramTenthVoltages;
}

uint16_t CompaqIpaqH3000BatteryCurve::VoltageFor(int fill_percent) const {
    const int tenth = std::clamp((fill_percent + 5) / 10, 1, 10);
    return (*tenth_voltages_)[static_cast<size_t>(tenth - 1)];
}

}
