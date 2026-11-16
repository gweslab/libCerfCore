#pragma once

#include "../../core/unreachable.h"
#include "imx6_vivante_state.h"
#include "imx6_variant.h"

#include <cstdint>

namespace cerfcore {

namespace imx6_vivante {

struct VivanteIdentityProfile {
    uint32_t chip_identity;
    uint32_t model;
    uint32_t revision;
    uint32_t features;
    uint32_t minor[6];
    uint32_t specs[4];
};

inline constexpr uint32_t kChipIdentityStub = 0u;
inline constexpr uint32_t kGc320ChipTimeStub = 0x02062400u;
inline constexpr uint32_t kMinorFeatures4Stub = 0u;
inline constexpr uint32_t kMinorFeatures5Stub = 0u;
inline constexpr uint32_t kGc320Specs3 = 0x00000081u;
inline constexpr uint32_t kGc2000Specs3 = 0x000000C1u;
inline constexpr uint32_t kGc880Specs3 = 0x000000C1u;
inline constexpr uint32_t kUserModeReadStub = 0u;
inline constexpr uint32_t kVivanteFeaturePipe2d = 1u << 9u;
inline constexpr uint32_t kVivanteMinorFeatureNonPowerOfTwo = 1u << 21u;

inline constexpr VivanteIdentityProfile kGc320Identity = {
    kChipIdentityStub,
    0x0320u,
    0x5007u,
    0xE02C7ECAu,
    {0xC1399EFFu, 0x020FB2DBu, 0u, 0u, kMinorFeatures4Stub, kMinorFeatures5Stub},
    {0xA4410A61u, 0x01000008u, kGc320Specs3, 0u},
};

inline constexpr VivanteIdentityProfile kGc2000Identity = {
    kChipIdentityStub,
    0x2000u,
    0x5108u,
    0xE0296CADu,
    {0xC9799EFFu, 0x2EFBF2D9u, 0u, 0u, kMinorFeatures4Stub, kMinorFeatures5Stub},
    {0x94410A68u, 0u, kGc2000Specs3, 0u},
};

inline constexpr VivanteIdentityProfile kGc880Identity = {
    kChipIdentityStub,
    0x0880u,
    0x5106u,
    0xE02864ADu,
    {0xC1F99EFFu, 0xEEFBF2D9u, 0x02100284u, 0u, kMinorFeatures4Stub, kMinorFeatures5Stub},
    {0x92108868u, 0x01000000u, kGc880Specs3, 0u},
};

inline constexpr VivanteIdentityProfile kGc355Identity = {
    kChipIdentityStub, 0x0355u, 0x1215u, 0x1E2C7CC8u,
    {0x0024207Fu, 0u, 0x00010104u, 0u, kMinorFeatures4Stub, kMinorFeatures5Stub}, {0u, 0u, 0u, 0u},
};

static_assert((kGc320Identity.features & kVivanteFeaturePipe2d) != 0u);
static_assert((kGc2000Identity.features & kVivanteFeaturePipe2d) == 0u);
static_assert((kGc880Identity.features & kVivanteFeaturePipe2d) == 0u);
static_assert((kGc355Identity.features & kVivanteFeaturePipe2d) == 0u);
static_assert((kGc320Identity.minor[1] & (1u << 28)) == 0u);
static_assert((kGc2000Identity.minor[1] & (1u << 28)) == 0u);
static_assert((kGc880Identity.minor[1] & (1u << 28)) == 0u);
static_assert((kGc355Identity.minor[1] & (1u << 28)) == 0u);

inline const VivanteIdentityProfile& IdentityFor(VivanteCore core, Imx6Family family) {
    switch (core) {
    case VivanteCore::Gpu3d:
        switch (family) {
        case Imx6Family::SoloDualLite: return kGc880Identity;
        case Imx6Family::DualQuad: return kGc2000Identity;
        }
        CERF_UNREACHABLE();
    case VivanteCore::Gc3202d: return kGc320Identity;
    case VivanteCore::Gc355Vg:
        if (family == Imx6Family::DualQuad) return kGc355Identity;
        CERF_UNREACHABLE();
    }
    CERF_UNREACHABLE();
}

}

}
