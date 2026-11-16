#pragma once

#include <cstdint>
#include <vector>

namespace cerfcore {

namespace imx6_vivante {

enum class VivanteCore : uint8_t {
    Gpu3d,
    Gc3202d,
    Gc355Vg,
};

enum FeOpcode : uint32_t {
    kFeLoadState = 1,
    kFeEnd = 2,
    kFeNop = 3,
    kFeDraw2d = 4,
    kFeDrawPrimitives = 5,
    kFeDrawIndexedPrimitives = 6,
    kFeWait = 7,
    kFeLink = 8,
    kFeStall = 9,
    kFeCall = 10,
    kFeReturn = 11,
    kFeDrawInstanced = 12,
    kFeChipSelect = 13,
    kFeWaitFence = 15,
    kFeDrawIndirect = 16,
    kFeSnapPages = 19,
};

inline constexpr uint32_t kFeCommandEnable = 0x00010000u;
inline constexpr uint32_t kFeCommandPrefetchMask = 0x0000FFFFu;
inline constexpr uint32_t kFeCallStackDepth = 16u; /* Real depth not documented, 16 is enough for the guest. */
inline constexpr uint32_t kMaxStateBytes = 0x20000u;

inline constexpr uint32_t FePrefetchDwords(uint32_t prefetch64) {
    return prefetch64 * 2u;
}

inline constexpr uint32_t FeAlignedCommandWords(uint32_t semantic_words) {
    return (semantic_words + 1u) & ~1u;
}

inline constexpr uint32_t FeDraw2dPacketWords(uint32_t rectangle_count, uint32_t data_count) {
    return 2u + rectangle_count * 2u + FeAlignedCommandWords(data_count);
}
inline constexpr uint32_t kMaxFeCommandWords = FeDraw2dPacketWords(256u, 2047u);
inline constexpr uint32_t kMmuv1GpuMemStart = 0x80000000u;
inline constexpr uint32_t kMmuv1PageMask = 0xFFFFF000u;
inline constexpr uint32_t kMmuv1FePageTable = 0x400u;
inline constexpr uint32_t kMmuv1TxPageTable = 0x404u;
inline constexpr uint32_t kMmuv1PePageTable = 0x408u;
inline constexpr uint32_t kMmuv1RaPageTable = 0x410u;
inline constexpr uint32_t kMmuv1MemoryBaseRa = 0x418u;
inline constexpr uint32_t kMmuv1MemoryBaseFe = 0x41Cu;
inline constexpr uint32_t kMmuv1MemoryBaseTx = 0x420u;
inline constexpr uint32_t kMmuv1MemoryBasePe = 0x428u;
inline constexpr uint32_t kIdleStateBits = 0x8007FE00u;
inline constexpr uint32_t kFeCommandGuestBit21 = 1u << 21;
inline constexpr char kRopInputNotFetched[] = "kRopInputNotFetched";
inline constexpr char kNegativeRelativeSourceOrigin[] = "kNegativeRelativeSourceOrigin";
inline constexpr char kAlphaBlendSourceWithoutSurface[] = "kAlphaBlendSourceWithoutSurface";
inline constexpr char kEngineCacheFlush[] = "kEngineCacheFlush";

enum class MmuClient : uint8_t {
    Fe = 0,
    Texture = 1,
    PixelEngine = 2,
    Rasterizer = 3,
};

struct FeStats {
    bool idle_ring = false;
    bool stopped = false;
    bool blocked = false;
};

struct FeCallFrame {
    uint32_t return_address = 0;
    uint32_t return_window_words = 0;
};

struct IdleRingInfo {
    uint32_t target = 0;
    uint32_t prefetch = 0;
};

struct VivanteState {
    uint32_t regs_[0x4000u / 4u]{};
    uint32_t intr_status_ = 0;
    uint32_t intr_enable_ = 0;
    bool irq_asserted_ = false;
    bool fe_live_ = false;
    bool fe_idle_ring_ = false;
    bool fe_stalled_ = false;
    bool fe_waiting_ = false;
    uint32_t fe_wait_cycles_ = 0u;
    uint32_t fe_ring_pc_ = 0;
    uint32_t fe_ring_prefetch_ = 0;
    uint32_t fe_window_words_ = 0;
    FeCallFrame fe_call_stack_[kFeCallStackDepth]{};
    uint32_t fe_call_depth_ = 0;
    uint32_t semaphore_tokens_[32]{};
    std::vector<uint32_t> state_;
    bool pattern_init_valid_ = false;
    uint32_t pattern_init_config_ = 0u;
    uint32_t pattern_init_address_ = 0u;
    uint32_t pattern_init_low_ = 0u;
    uint32_t pattern_init_high_ = 0u;
    uint32_t pattern_init_mask_low_ = 0u;
    uint32_t pattern_init_mask_high_ = 0u;
    uint32_t pattern_init_byte_count_ = 0u;
    uint8_t pattern_init_bytes_[256]{};
};

}

}
