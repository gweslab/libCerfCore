#include "imx6_vivante_mem.h"
#include "imx6_vivante_blit_ops.h"
#include "imx6_vivante_state_registers.h"

namespace cerfcore {

namespace imx6_vivante {

namespace {

constexpr char kClearByteMask[] = "kClearByteMask";
constexpr char kClearPixelValueLow[] = "kClearPixelValueLow";
constexpr char kClearPixelValueHigh[] = "kClearPixelValueHigh";
constexpr char kColorStatusBase[] = "kColorStatusBase";
constexpr char kColorSurfaceBase[] = "kColorSurfaceBase";
constexpr char kColorClearValue[] = "kColorClearValue";
constexpr VivanteMem::MaskedStateGroup kVrKickMask[] = {{0x00000003u, 1u << 3}};
constexpr VivanteMem::MaskedStateGroup kPeConfigMask[] = {{0x00000003u, 1u << 3}};

}

bool VivanteStateRegisters::SupportsOffset(uint32_t off) {
    if ((off & 3u) != 0u) return false;
    if ((off >= 0x01800u && off <= 0x01930u) ||
        (off >= 0x02800u && off <= 0x02930u) ||
        (off >= 0x02A00u && off <= 0x02B30u) ||
        (off >= 0x03400u && off <= 0x037FCu)) return true;
    static constexpr uint32_t multi_bases[] = {
        0x12800u, 0x12810u, 0x12820u, 0x12830u, 0x12840u,
        0x12850u, 0x12860u, 0x12870u, 0x12880u, 0x12890u,
        0x128E0u, 0x128F0u, 0x12900u, 0x12910u, 0x12920u,
        0x12930u, 0x12940u, 0x12950u, 0x12960u, 0x12970u,
    };
    for (uint32_t base : multi_bases)
        if (off >= base && off <= base + 0x0Cu) return true;
    switch (off) {
    case 0x007E8u: case 0x007F4u:
    case 0x01200u: case 0x01204u: case 0x01208u: case 0x0120Cu: case 0x01210u: case 0x01214u:
    case 0x01218u: case 0x0121Cu: case 0x01220u: case 0x01224u: case 0x01228u: case 0x0122Cu:
    case 0x01230u: case 0x01234u: case 0x01238u: case 0x0123Cu: case 0x01240u: case 0x01244u:
    case 0x01248u: case 0x0124Cu: case 0x01250u: case 0x01254u: case 0x0125Cu: case 0x01260u:
    case 0x01264u: case 0x01268u: case 0x01270u: case 0x01274u: case 0x0127Cu: case 0x01280u:
    case 0x01284u: case 0x01288u: case 0x0128Cu: case 0x01290u: case 0x01294u: case 0x01298u:
    case 0x0129Cu: case 0x012A0u: case 0x012A4u: case 0x012A8u: case 0x012ACu: case 0x012B4u:
    case 0x012B8u: case 0x012BCu: case 0x012C0u: case 0x012C4u: case 0x012C8u: case 0x012CCu:
    case 0x012D0u: case 0x012D4u: case 0x012D8u: case 0x012DCu: case 0x012E0u: case 0x012E4u:
    case 0x012F0u: case 0x01300u: case 0x01304u: case 0x01308u: case 0x0130Cu:
    case 0x01600u: case 0x01604u: case 0x01608u: case 0x0160Cu: case 0x01610u: case 0x01614u:
    case 0x01620u: case 0x0163Cu: case 0x01640u: case 0x01644u: case 0x01648u: case 0x0164Cu:
    case 0x01650u: case 0x01654u: case 0x01658u: case 0x0165Cu: case 0x01660u: case 0x016B0u:
    case 0x016C0u: case 0x016C4u: case 0x016E0u: case 0x016E4u: case 0x01700u: case 0x01704u:
    case 0x03800u: case 0x03804u: case 0x03808u: case 0x0380Cu: case 0x03C00u:
        return true;
    default:
        return false;
    }
}

void VivanteStateRegisters::Store(uint32_t byte_off, uint32_t value) {
    const uint32_t idx = byte_off >> 2;
    if (idx >= state_.state_.size()) {
        memory_.HaltUnsupported("imx6-vivante state store out of range", byte_off, value);
    }

    if (byte_off == 0x007E8u || byte_off == 0x007F4u) {
        memory_.HaltUnsupported("kFenceWaitDataWrite", byte_off, value);
    }

    switch (byte_off) {
    case 0x01268u: memory_.HaltUnsupported(kClearByteMask, byte_off, value);
    case 0x01270u: memory_.HaltUnsupported(kClearPixelValueLow, byte_off, value);
    case 0x01274u: memory_.HaltUnsupported(kClearPixelValueHigh, byte_off, value);
    case 0x01658u: memory_.HaltUnsupported(kColorStatusBase, byte_off, value);
    case 0x0165Cu: memory_.HaltUnsupported(kColorSurfaceBase, byte_off, value);
    case 0x01660u: memory_.HaltUnsupported(kColorClearValue, byte_off, value);
    default: break;
    }

    if (byte_off == 0x00004u) {
        if (value != 0u) memory_.HaltUnsupported("imx6-vivante write to IDLE_STATE", byte_off, value);
        return;
    }
    if (byte_off == 0x03800u) {
        if ((value & ~1u) != 0u) {
            memory_.HaltUnsupported("imx6-vivante unsupported PIPE_SELECT bits", byte_off, value);
        }
        state_.state_[idx] = value;
        return;
    }
    if (byte_off == 0x03804u) {
        if ((value & 0xFFFFE000u) != 0u)
            memory_.HaltUnsupported("imx6-vivante unsupported GL.EVENT bits", byte_off, value);
        state_.state_[idx] = value;
        memory_.RaiseEvent(value & 0x1Fu);
        return;
    }
    if (byte_off == 0x012E8u || byte_off == 0x012ECu) {
        if (value != 0xFFFFFFFFu) memory_.HaltUnsupported("imx6-vivante dither", byte_off, value);
        state_.state_[idx] = value;
        return;
    }
    if (byte_off == 0x0123Cu) {
        state_.state_[idx] = value;
        if ((value & 0xC0u) != 0u) {
            state_.pattern_init_valid_ = true;
            state_.pattern_init_config_ = value & ~0xC0u;
            state_.pattern_init_address_ = memory_.StateReg(0x01238u);
            state_.pattern_init_low_ = memory_.StateReg(0x01240u);
            state_.pattern_init_high_ = memory_.StateReg(0x01244u);
            state_.pattern_init_mask_low_ = memory_.StateReg(0x01248u);
            state_.pattern_init_mask_high_ = memory_.StateReg(0x0124Cu);
            state_.pattern_init_byte_count_ = 0u;
            if ((value & (1u << 4)) != 0u) {
                const uint32_t bpp = VivanteBlitOps::PatternBytesPerPixel(memory_, value);
                if (bpp == 0u || bpp > 4u || state_.pattern_init_address_ == 0u ||
                    !memory_.ReadGpuBytes(state_.pattern_init_address_, state_.pattern_init_bytes_, 64u * bpp))
                    memory_.HaltUnsupported("PATTERN_CONFIG INIT_TRIGGER memory pattern unavailable",
                                            state_.pattern_init_address_, value);
                state_.pattern_init_byte_count_ = 64u * bpp;
            }
        }
        return;
    }
    if (byte_off == 0x01324u) {
        if (value != 0x00030003u) memory_.HaltUnsupported("imx6-vivante state 0x1324", byte_off, value);
        return;
    }
    if (byte_off == 0x01720u) {
        if (value != 0u) memory_.HaltUnsupported("imx6-vivante sampler tile status", byte_off, value);
        return;
    }
    if (byte_off == 0x03818u) {
        if (!memory_.Is2d() || value != 2u) memory_.HaltUnsupported("imx6-vivante multisample config", byte_off, value);
        state_.state_[idx] = value;
        return;
    }
    if (byte_off == 0x0130Cu) {
        if (value != 0u) {
            memory_.HaltUnsupported("imx6-vivante unsupported DEYUV conversion", byte_off, value);
        }
        state_.state_[idx] = 0u;
        return;
    }

    if (byte_off == 0x0380Cu) {
        if ((value & ~0x7Fu) != 0u)
            memory_.HaltUnsupported(kEngineCacheFlush, byte_off, value);
        return;
    }

    if (byte_off == 0x01650u) {
        if ((value & ~1u) != 0u)
            memory_.HaltUnsupported(kEngineCacheFlush, byte_off, value);
        return;
    }

    if (byte_off == 0x03810u) {
        if ((value & ~0x1Fu) != 0u)
            memory_.HaltUnsupported("imx6-vivante unsupported FLUSH_MMU bits", byte_off, value);
        return;
    }

    if (byte_off == 0x01654u) {
        memory_.HaltUnsupported("imx6-vivante TS_MEM_CONFIG write", byte_off, value);
    }

    if (byte_off == 0x03808u) {
        state_.state_[idx] = value;
        memory_.ArmSemaphoreToken(value);
        memory_.NotifySemaphoreTokenArmed();
        return;
    }

    if (byte_off == 0x03C00u) {
        state_.state_[idx] = value;
        if (!memory_.TryConsumeSemaphoreToken(value))
            memory_.HaltUnsupported("kStateLoadStallBlocking", byte_off, value);
        return;
    }

    if (byte_off == 0x01294u) {
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, kVrKickMask);
        return;
    }

    if (byte_off == 0x012B0u) {
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, kPeConfigMask);
        return;
    }

    if ((byte_off >= 0x12930u && byte_off < 0x12940u) || byte_off == 0x012D4u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000333u, 1u << 12},
            {0x03330000u, 1u << 28},
            {0x20000000u, 1u << 31},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if ((byte_off >= 0x12940u && byte_off < 0x12950u) || byte_off == 0x012D8u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},
            {0x00000010u, 1u << 7},
            {0x00000100u, 1u << 11},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012E4u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000003u, 1u << 3},
            {0x000000F0u, 1u << 8},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (byte_off == 0x012F0u) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000001u, 1u << 3},
            {0x00000010u, 1u << 7},
            {0x00000100u, 1u << 11},
            {0x00001000u, 1u << 15},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if ((byte_off >= 0x128F0u && byte_off < 0x12900u) || byte_off == 0x012BCu) {
        static constexpr VivanteMem::MaskedStateGroup groups[] = {
            {0x00000007u, 1u << 8},
            {0x00000038u, 1u << 9},
            {0x00003000u, 1u << 15},
            {0x00030000u, 1u << 19},
        };
        state_.state_[idx] = memory_.MergeMaskedState(state_.state_[idx], value, groups);
        return;
    }

    if (SupportsOffset(byte_off)) {
        state_.state_[idx] = value;
        return;
    }
    if (byte_off >= 0x12A60u && byte_off < 0x12A80u) {
        if (value != 0u) memory_.HaltUnsupported("imx6-vivante BLOCK8 source config", byte_off, value);
        state_.state_[idx] = value;
        return;
    }
    memory_.HaltUnsupported("imx6-vivante unmodelled state register", byte_off, value);
}

}

}
