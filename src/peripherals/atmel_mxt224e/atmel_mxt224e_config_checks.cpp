#include "atmel_mxt224e_config_checks.h"

#include "../../core/board_parts.h"
#include "atmel_mxt224e_part_id.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint16_t kT9YOrigin = kMxtT9Start + 2u;
constexpr uint16_t kT9XSize   = kMxtT9Start + 3u;
constexpr uint16_t kT9YSize   = kMxtT9Start + 4u;
constexpr uint16_t kT25Cmd    = kMxtT25Start + 1u;
constexpr uint16_t kT46Ctrl   = kMxtT46Start;

constexpr uint8_t kT9DisAmp     = 0x04u;
constexpr uint8_t kT9DisVect    = 0x08u;
constexpr uint8_t kT18DisMntr   = 0x80u;

constexpr uint8_t  kT7IntervalMin = 4u;
constexpr uint8_t  kModeMax       = 6u;
constexpr uint8_t  kNativeXLines  = 16u;
constexpr uint8_t  kNativeLines   = 16u + 14u;
constexpr uint16_t kRangeMin      = 127u;
constexpr uint16_t kRangeMax      = 4095u;
constexpr uint8_t  kMinSize10Bit  = 3u;
constexpr uint8_t  kMinSize12Bit  = 7u;

constexpr std::array<uint8_t, 8> kUnmodeledEnables = {kMxtT15, kMxtT19, kMxtT23, kMxtT25,
                                                      kMxtT40, kMxtT42, kMxtT47, kMxtT48};

uint8_t At(const AtmelMxt224eConfigChecks::Config& config, uint16_t addr) { return config[addr - kMxtNvmStart]; }

}

bool AtmelMxt224eConfigChecks::ShouldRegister() { return emu_.Get<BoardParts>().Has(PartId::AtmelMxt224e); }

void AtmelMxt224eConfigChecks::RequireModeled(const Config& config) const {
    auto& fatal = emu_.Get<Fatal>();
    for (const AtmelMxt224eObject& o : kMxtObjects) {
        if (std::find(kUnmodeledEnables.begin(), kUnmodeledEnables.end(), o.type) == kUnmodeledEnables.end()) {
            continue;
        }
        if ((At(config, o.start) & kMxtCtrlEnable) != 0u) {
            fatal.Die("atmel mxt224e: T%u is enabled (CTRL 0x%02X)", o.type, At(config, o.start));
        }
    }
    if (At(config, kT25Cmd) != 0u) fatal.Die("atmel mxt224e: T25 command 0x%02X", At(config, kT25Cmd));
    if (At(config, kT46Ctrl) != 0u) fatal.Die("atmel mxt224e: T46 CTRL 0x%02X", At(config, kT46Ctrl));
    if (At(config, kMxtT46Mode) > kModeMax) fatal.Die("atmel mxt224e: T46 MODE %u", At(config, kMxtT46Mode));
    if ((At(config, kMxtT18Ctrl) & ~(kMxtT18Mode | kT18DisMntr)) != 0u) {
        fatal.Die("atmel mxt224e: T18 CTRL 0x%02X", At(config, kMxtT18Ctrl));
    }
    if (At(config, kMxtT18Command) > kMxtT18ChgLow) {
        fatal.Die("atmel mxt224e: T18 COMMAND %u", At(config, kMxtT18Command));
    }
    const uint8_t t9      = At(config, kMxtT9Ctrl);
    const bool    enabled = (t9 & kMxtCtrlEnable) != 0u;
    if (enabled && (t9 & kMxtT9Rpten) != 0u && (t9 & (kT9DisAmp | kT9DisVect)) != (kT9DisAmp | kT9DisVect)) {
        fatal.Die("atmel mxt224e: T9 reports amplitude or vector changes (CTRL 0x%02X)", t9);
    }
    const uint8_t touches = At(config, kMxtT9NumTouch);
    if (enabled && (touches == 0u || touches > kMxtT9Touches)) fatal.Die("atmel mxt224e: T9 NUMTOUCH %u", touches);
    for (const uint16_t field : {kMxtT7IdleAcqInt, kMxtT7ActvAcqInt}) {
        if (At(config, field) != 0u && At(config, field) < kT7IntervalMin) {
            fatal.Die("atmel mxt224e: T7 byte %u holds acquisition interval %u", field - kMxtT7Start,
                      At(config, field));
        }
    }
}

bool AtmelMxt224eConfigChecks::ConfigError(const Config& config, uint8_t active_mode) const {
    if ((At(config, kMxtT9Ctrl) & kMxtCtrlEnable) == 0u) return false;
    const uint32_t x_lines   = kNativeXLines + active_mode;
    const uint32_t y_lines   = kNativeLines - x_lines;
    const uint16_t x_range   = cerf::le::U16(config.data() + kMxtT9XRange - kMxtNvmStart);
    const uint16_t y_range   = cerf::le::U16(config.data() + kMxtT9YRange - kMxtNvmStart);
    const auto     bad_range = [](uint16_t r) { return r > kRangeMax || (r != 0u && r < kRangeMin); };
    if (bad_range(x_range) || bad_range(y_range)) return true;
    const uint8_t  x_min = MxtRange12Bit(MxtEffectiveRange(x_range)) ? kMinSize12Bit : kMinSize10Bit;
    const uint8_t  y_min = MxtRange12Bit(MxtEffectiveRange(y_range)) ? kMinSize12Bit : kMinSize10Bit;
    const uint8_t  x_size = At(config, kT9XSize);
    const uint8_t  y_size = At(config, kT9YSize);
    const uint32_t x_end = static_cast<uint32_t>(At(config, kMxtT9XOrigin)) + x_size;
    const uint32_t y_end = static_cast<uint32_t>(At(config, kT9YOrigin)) + y_size;
    return x_end > x_lines || y_end > y_lines || x_size < x_min || y_size < y_min;
}

REGISTER_SERVICE(AtmelMxt224eConfigChecks);

}
