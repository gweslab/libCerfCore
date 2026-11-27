#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace cerfcore {

inline constexpr uint32_t kMsm8255AdspModuleAfe        = 0x0106DD6Fu;
inline constexpr uint32_t kMsm8255AdspModuleAudPlay0   = 0x0106DD70u;
inline constexpr uint32_t kMsm8255AdspModuleAudPlay1   = 0x0106DD71u;
inline constexpr uint32_t kMsm8255AdspModuleAudPp      = 0x0106DD72u;
inline constexpr uint32_t kMsm8255AdspModuleAudPreProc = 0x0106DD7Cu;
inline constexpr uint32_t kMsm8255AdspModuleAudPlay2   = 0x0106DD91u;
inline constexpr uint32_t kMsm8255AdspModuleAudPlay3   = 0x0106DD92u;
inline constexpr uint32_t kMsm8255AdspModuleAudPlay4   = 0x0106DD93u;
inline constexpr uint32_t kMsm8255AdspModuleAudRec0    = 0x0109696Fu;
inline constexpr uint32_t kMsm8255AdspModuleAudRec1    = 0x01096970u;
inline constexpr uint32_t kMsm8255AdspModuleAudRec2    = 0x010A2F59u;

inline constexpr std::array<uint32_t, 11> kMsm8255AdspModules = {
    kMsm8255AdspModuleAfe,      kMsm8255AdspModuleAudPlay0, kMsm8255AdspModuleAudPlay1,
    kMsm8255AdspModuleAudPp,    kMsm8255AdspModuleAudPreProc, kMsm8255AdspModuleAudPlay2,
    kMsm8255AdspModuleAudPlay3, kMsm8255AdspModuleAudPlay4, kMsm8255AdspModuleAudRec0,
    kMsm8255AdspModuleAudRec1,  kMsm8255AdspModuleAudRec2,
};

inline constexpr uint32_t kMsm8255AdspWidth16 = 16u;
inline constexpr uint32_t kMsm8255AdspWidth32 = 32u;

struct Msm8255AdspQueue {
    uint32_t module = 0;
    uint32_t queue  = 0;
    uint32_t width  = 0;
};

inline constexpr std::array<Msm8255AdspQueue, 17> kMsm8255AdspQueues = {{
    {kMsm8255AdspModuleAfe, 33u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudPp, 10u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudPp, 11u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudPp, 12u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudPlay0, 13u, kMsm8255AdspWidth32},
    {kMsm8255AdspModuleAudPlay1, 14u, kMsm8255AdspWidth32},
    {kMsm8255AdspModuleAudPlay2, 15u, kMsm8255AdspWidth32},
    {kMsm8255AdspModuleAudPlay3, 16u, kMsm8255AdspWidth32},
    {kMsm8255AdspModuleAudPlay4, 17u, kMsm8255AdspWidth32},
    {kMsm8255AdspModuleAudPreProc, 18u, kMsm8255AdspWidth32},
    {kMsm8255AdspModuleAudPreProc, 35u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudRec0, 36u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudRec0, 37u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudRec1, 38u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudRec1, 39u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudRec2, 41u, kMsm8255AdspWidth16},
    {kMsm8255AdspModuleAudRec2, 42u, kMsm8255AdspWidth16},
}};

inline constexpr int kMsm8255AdspNoModule = -1;

inline constexpr int Msm8255AdspModuleIndex(uint32_t module) {
    for (size_t i = 0; i < kMsm8255AdspModules.size(); ++i) {
        if (kMsm8255AdspModules[i] == module) return static_cast<int>(i);
    }
    return kMsm8255AdspNoModule;
}

inline constexpr uint32_t kMsm8255AdspImage           = 0u;
inline constexpr uint32_t kMsm8255AdspImageCount      = 1u;
inline constexpr uint32_t kMsm8255AdspMaxCommandWords = 512u;

}
