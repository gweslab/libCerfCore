#pragma once

#include "imx6_ipu_internal_mem.h"
#include "imx6_ipu_lut.h"

#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>

namespace cerfcore {

struct Imx6IpuChannelDesc {
    uint32_t eba = 0;
    uint32_t eba0 = 0;
    uint32_t eba1 = 0;
    uint32_t fw = 0;
    uint32_t fh = 0;
    uint32_t sl = 0;
    uint32_t bpp_code = 0;
    uint32_t bits_per_pixel = 0;
    uint32_t bytes_per_pixel = 0;
    uint32_t pfs = 0;
    uint32_t dec_sel = 0;
    bool separate_alpha = false;
    std::array<uint32_t, 4> component_width{};
    std::array<uint32_t, 4> component_offset{};
    bool valid = false;
};

class Imx6IpuCpmem : public cerf_imx6_ipu_mem_detail::Imx6IpuInternalMem<0x02700000u> {
public:
    using Imx6IpuInternalMem<0x02700000u>::Imx6IpuInternalMem;

    uint8_t ReadByte(uint32_t addr) override;
    uint16_t ReadHalf(uint32_t addr) override;
    uint32_t ReadWord(uint32_t addr) override;
    void WriteByte(uint32_t addr, uint8_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;
    void WriteWord(uint32_t addr, uint32_t value) override;

    Imx6IpuChannelDesc DecodeChannel(uint32_t ch) const;
    void ResetMemories(uint32_t enable_mask) {
        if (enable_mask & (1u << 0)) ClearMemory(0x40000u, 0x118u);
        if (enable_mask & (1u << 2)) {
            ClearMemory(0x00000u, 0x1400u);
            for (auto& buffer : current_buffer_) buffer = 0u;
        }
        if (enable_mask & (1u << 3)) ClearMemory(0x60000u, 0x10000u);
        if (enable_mask & (1u << 20)) ClearMemory(0x80000u, 0x800u);
        constexpr uint32_t lut_mask = (1u << 9) | (1u << 10);
        const uint32_t lut_selection = enable_mask & lut_mask;
        if (lut_selection == (1u << 9))
            emu_.Get<Fatal>().Die("[IPU] IPUx_MEM_RST cannot map RST_MEM_EN lut0 alone to LUT words");
        if (lut_selection == (1u << 10))
            emu_.Get<Fatal>().Die("[IPU] IPUx_MEM_RST cannot map RST_MEM_EN lut1 alone to LUT words");
        if (lut_selection == lut_mask) emu_.Get<Imx6IpuLut>().Reset();
    }
    void SetCurrentBuffer(uint32_t ch, uint32_t buffer) {
        if (ch < 64u) current_buffer_[ch] = buffer & 1u;
    }

    void SaveState(StateWriter& w) override {
        Imx6IpuInternalMem::SaveState(w);
        w.WriteBytes("current_buffer", current_buffer_, sizeof(current_buffer_));
        emu_.Get<Imx6IpuLut>().SaveState(w);
    }
    void RestoreState(StateReader& r) override {
        Imx6IpuInternalMem::RestoreState(r);
        r.ReadBytes("current_buffer", current_buffer_, sizeof(current_buffer_));
        emu_.Get<Imx6IpuLut>().RestoreState(r);
    }

private:
    uint32_t Field(uint32_t ch, uint32_t word, uint32_t off, uint32_t len) const;
    void RequireLutIdle(const char* operation) const;
    uint8_t current_buffer_[64]{};
};

}
