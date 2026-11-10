#pragma once

#include <cstdint>

namespace cerfcore {

struct Pr31x00ChipSelectWindow {
    uint32_t base;
    uint32_t end;
    uint32_t cs;
    uint32_t enable;
};

inline constexpr uint32_t kPr31x00Cs0KsegBase  = 0x11000000u;
inline constexpr uint32_t kPr31x00Cs0KsegEnd   = 0x20000000u;
inline constexpr uint32_t kPr31x00Cs1KsegBase  = 0x10000000u;
inline constexpr uint32_t kPr31x00Cs1KsegEnd   = 0x10400000u;
inline constexpr uint32_t kPr31x00Cs1KusegBase = 0x58000000u;
inline constexpr uint32_t kPr31x00Cs1KusegEnd  = 0x5C000000u;

inline constexpr uint32_t kPr31x00EnCs1User = 1u << 21;
inline constexpr uint32_t kPr31x00EnCs2User = 1u << 22;
inline constexpr uint32_t kPr31x00EnCs3User = 1u << 23;

inline constexpr Pr31x00ChipSelectWindow kPr31x00ChipSelectWindows[] = {
    {kPr31x00Cs0KsegBase, kPr31x00Cs0KsegEnd, 0u, 0u},
    {0x50000000u, 0x58000000u, 0u, 0u},
    {kPr31x00Cs1KsegBase, kPr31x00Cs1KsegEnd, 1u, 0u},
    {kPr31x00Cs1KusegBase, kPr31x00Cs1KusegEnd, 1u, kPr31x00EnCs1User},
    {0x10400000u, 0x10800000u, 2u, 0u},
    {0x5C000000u, 0x60000000u, 2u, kPr31x00EnCs2User},
    {0x10800000u, 0x10C00000u, 3u, 0u},
    {0x60000000u, 0x64000000u, 3u, kPr31x00EnCs3User},
};

}
