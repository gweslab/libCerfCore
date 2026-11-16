#include "../freescale_sdma_channel_zero.h"
#include "../freescale_sdma_regs.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_stream.h>

#include <cstring>
#include "imx6_id.h"

namespace cerfcore {

using namespace cerf_freescale_sdma_detail;

namespace {

constexpr uint32_t kSdmaProgramRamFirstWord = 0x1000u;

class Imx6SdmaChannel0 final : public FreescaleSdmaChannelZero {
public:
    using FreescaleSdmaChannelZero::FreescaleSdmaChannelZero;

    bool ShouldRegister() override;
    void OnReady() override;

    void Run(const uint8_t* bd, uint32_t running_channels, uint32_t sdma_mmio_base) override;
    bool ContextWatermark(uint32_t channel, uint32_t& value) const override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;
    void Reset() override;

private:
    void CopyIn(uint32_t* destination, uint32_t arm_pa, uint32_t count, const char* what);
    static bool OverlapsContext(uint32_t first_word, uint32_t count, uint32_t channel);

    uint32_t context_loaded_ = 0;
    uint32_t ram_[cerf_freescale_sdma_detail::kSdmaDataRamEndWord - cerf_freescale_sdma_detail::kSdmaDataRamFirstWord] = {};
};


bool Imx6SdmaChannel0::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

void Imx6SdmaChannel0::OnReady() {
    Reset();
}

void Imx6SdmaChannel0::SaveState(StateWriter& writer) const {
    writer.Write("context_loaded", context_loaded_);
    writer.WriteBytes("ram", ram_, sizeof(ram_));
}

void Imx6SdmaChannel0::RestoreState(StateReader& reader) {
    reader.Read("context_loaded", context_loaded_);
    reader.ReadBytes("ram", ram_, sizeof(ram_));
}

void Imx6SdmaChannel0::Reset() {
    context_loaded_ = 0;
    std::memset(ram_, 0, sizeof(ram_));
}

bool Imx6SdmaChannel0::ContextWatermark(uint32_t channel, uint32_t& value) const {
    if ((context_loaded_ & (1u << channel)) == 0u) return false;
    value = ram_[kSdmaContextBase - kSdmaDataRamFirstWord + kSdmaContextWords * channel + kContextGr7Word];
    return true;
}

bool Imx6SdmaChannel0::OverlapsContext(uint32_t first_word, uint32_t count, uint32_t channel) {
    const uint32_t context_first = kSdmaContextBase + kSdmaContextWords * channel;
    return first_word < context_first + kSdmaContextWords && uint64_t{first_word} + count > context_first;
}

void Imx6SdmaChannel0::CopyIn(uint32_t* destination, uint32_t arm_pa, uint32_t count, const char* what) {
    auto& memory = emu_.Get<EmulatedMemory>();
    for (uint32_t copied = 0; copied < count; ++copied) {
        uint8_t* src = memory.TryTranslateRange(arm_pa + copied * 4u, sizeof(uint32_t));
        if (!src) emu_.Get<Fatal>().Die("[SDMA] channel 0 %s source 0x%08X is not mapped", what, arm_pa + copied * 4u);
        destination[copied] = cerf::le::U32(src);
    }
}

void Imx6SdmaChannel0::Run(const uint8_t* bd, uint32_t running_channels, uint32_t sdma_mmio_base) {
    const uint32_t mode = cerf::le::U32(bd);
    const uint32_t arm_src_pa = cerf::le::U32(bd, 4u);
    const uint32_t sdma_dst_word = cerf::le::U32(bd, 8u);
    const uint32_t count = mode & 0xFFFFu;
    const uint32_t command = (mode >> 24) & 0xFFu;

    switch (command) {
    case kC0SetPm: {
        if (sdma_dst_word < kSdmaProgramRamFirstWord || uint64_t{sdma_dst_word} + count > kSdmaProgramWords)
            emu_.Get<Fatal>().Die("[SDMA] C0_SET_PM of %u words at 0x%04X runs past program RAM",
                                  count, sdma_dst_word);
        const uint32_t context_program_end =
            kSdmaProgramRamFirstWord + 2u * (kSdmaContextBase - kSdmaDataRamFirstWord + kChannelCount * kSdmaContextWords);
        if (sdma_dst_word < context_program_end)
            emu_.Get<Fatal>().Die("[SDMA] C0_SET_PM of %u words at 0x%04X overlaps the channel contexts; the "
                                  "half-word order of the shared RAM is not modelled", count, sdma_dst_word);
        return;
    }
    case kC0SetDm: {
        if (sdma_dst_word < kSdmaDataRamFirstWord || uint64_t{sdma_dst_word} + count > kSdmaDataRamEndWord)
            emu_.Get<Fatal>().Die("[SDMA] C0_SET_DM of %u words at 0x%04X is outside data RAM",
                                  count, sdma_dst_word);
        for (uint32_t channel = 0; channel < kChannelCount; ++channel) {
            if (!OverlapsContext(sdma_dst_word, count, channel)) continue;
            if ((running_channels & (1u << channel)) != 0u)
                emu_.Get<Fatal>().Die("[SDMA] C0_SET_DM over the context of running channel %u, HSTART 0x%08X",
                                      channel, sdma_mmio_base + kOffHstart);
            context_loaded_ |= 1u << channel;
        }
        CopyIn(ram_ + (sdma_dst_word - kSdmaDataRamFirstWord), arm_src_pa, count, "data");
        return;
    }
    default:
        break;
    }

    if ((command & 7u) != kC0SetCtx)
        emu_.Get<Fatal>().Die("Freescale SDMA: unsupported channel-0 command HSTART=0x%08X mode=0x%08X",
                              sdma_mmio_base + kOffHstart, mode);
    const uint32_t channel = command >> 3;
    if ((running_channels & (1u << channel)) != 0u)
        emu_.Get<Fatal>().Die("[SDMA] C0_SETCTX of running channel %u, HSTART 0x%08X", channel,
                              sdma_mmio_base + kOffHstart);
    if (count != kSdmaContextWords)
        emu_.Get<Fatal>().Die("[SDMA] C0_SETCTX of channel %u loads %u words, not %u", channel, count,
                              kSdmaContextWords);
    CopyIn(ram_ + (kSdmaContextBase - kSdmaDataRamFirstWord + kSdmaContextWords * channel), arm_src_pa, count, "context");
    context_loaded_ |= 1u << channel;
}

}

REGISTER_SERVICE_AS(Imx6SdmaChannel0, FreescaleSdmaChannelZero);

}
