#pragma once

#include "imx6_variant.h"

#include <cstddef>
#include <cstdint>

namespace cerfcore {

struct Imx6GpioPad {
    uint8_t bank;
    uint8_t pin;
    uint16_t mux_offset;
    uint16_t ctl_offset;
    uint8_t gpio_mode;
    uint32_t mux_reset;
    uint32_t ctl_reset;
};

struct Imx6GpioPadTable {
    const Imx6GpioPad* pads;
    size_t count;
};

extern const Imx6GpioPadTable kImx6DqGpioPadTable;
extern const Imx6GpioPadTable kImx6SdlGpioPadTable;

inline const Imx6GpioPadTable& Imx6GpioPadsFor(Imx6Family family) {
    return family == Imx6Family::SoloDualLite ? kImx6SdlGpioPadTable : kImx6DqGpioPadTable;
}

inline const Imx6GpioPad* FindImx6GpioPad(const Imx6GpioPadTable& table, uint32_t bank, uint32_t pin) {
    for (size_t i = 0; i < table.count; ++i)
        if (table.pads[i].bank == bank && table.pads[i].pin == pin) return &table.pads[i];
    return nullptr;
}

inline constexpr uint32_t kImx6PadMuxMode = 7u;
inline constexpr uint32_t kImx6PadCtlPke = 1u << 12;
inline constexpr uint32_t kImx6PadCtlPue = 1u << 13;
inline constexpr uint32_t kImx6PadCtlPus = 3u << 14;

struct Imx6PadInputLevel {
    bool known;
    bool high;
};

inline Imx6PadInputLevel ResolveImx6PadInputLevel(uint32_t ctl, bool keeper_level, bool keeper_known) {
    if ((ctl & kImx6PadCtlPke) == 0u) return {false, false};
    if ((ctl & kImx6PadCtlPue) != 0u) return {true, (ctl & kImx6PadCtlPus) != 0u};
    return {keeper_known, keeper_level};
}

}
