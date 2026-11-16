#include "imx6_ipu_cpmem.h"
#include "imx6_ipu.h"

#include <cerfcore/core/fatal.h>

#include <cstdint>

namespace cerfcore {

uint8_t Imx6IpuCpmem::ReadByte(uint32_t addr) {
    auto& lut = emu_.Get<Imx6IpuLut>();
    const uint32_t offset = addr - MmioBase();
    if (lut.ContainsOffset(offset)) {
        RequireLutIdle("read8");
        return lut.ReadByte(offset);
    }
    return Imx6IpuInternalMem::ReadByte(addr);
}

uint16_t Imx6IpuCpmem::ReadHalf(uint32_t addr) {
    auto& lut = emu_.Get<Imx6IpuLut>();
    const uint32_t offset = addr - MmioBase();
    if (lut.ContainsOffset(offset)) {
        RequireLutIdle("read16");
        return lut.ReadHalf(offset);
    }
    return Imx6IpuInternalMem::ReadHalf(addr);
}

uint32_t Imx6IpuCpmem::ReadWord(uint32_t addr) {
    auto& lut = emu_.Get<Imx6IpuLut>();
    const uint32_t offset = addr - MmioBase();
    if (lut.ContainsOffset(offset)) {
        RequireLutIdle("read32");
        return lut.ReadWord(offset);
    }
    return Imx6IpuInternalMem::ReadWord(addr);
}

void Imx6IpuCpmem::WriteByte(uint32_t addr, uint8_t value) {
    auto& lut = emu_.Get<Imx6IpuLut>();
    const uint32_t offset = addr - MmioBase();
    if (lut.ContainsOffset(offset)) {
        RequireLutIdle("write8");
        lut.WriteByte(offset, value);
        return;
    }
    Imx6IpuInternalMem::WriteByte(addr, value);
}

void Imx6IpuCpmem::WriteHalf(uint32_t addr, uint16_t value) {
    auto& lut = emu_.Get<Imx6IpuLut>();
    const uint32_t offset = addr - MmioBase();
    if (lut.ContainsOffset(offset)) {
        RequireLutIdle("write16");
        lut.WriteHalf(offset, value);
        return;
    }
    Imx6IpuInternalMem::WriteHalf(addr, value);
}

void Imx6IpuCpmem::WriteWord(uint32_t addr, uint32_t value) {
    auto& lut = emu_.Get<Imx6IpuLut>();
    const uint32_t offset = addr - MmioBase();
    if (lut.ContainsOffset(offset)) {
        RequireLutIdle("write32");
        lut.WriteWord(offset, value);
        return;
    }
    Imx6IpuInternalMem::WriteWord(addr, value);
}

void Imx6IpuCpmem::RequireLutIdle(const char* operation) const {
    const auto& ipu = emu_.Get<Imx6Ipu>();
    for (uint32_t channel = 0; channel < 64u; ++channel) {
        if (ipu.IdmacChannelEnabled(channel) && Field(channel, 1, 85, 4) == 5u)
            emu_.Get<Fatal>().Die("[IPU LUT] CPU %s blocked while IDMAC channel %u using PFS 5 is enabled",
                                  operation, channel);
    }
}

uint32_t Imx6IpuCpmem::Field(uint32_t ch, uint32_t word, uint32_t off, uint32_t len) const {
    const uint32_t base = ch * 16u + word * 8u;
    const uint32_t i = base + (off >> 5);
    const uint32_t bit = off & 31u;
    uint64_t v = static_cast<uint64_t>(regs_[i]) >> bit;
    if (bit + len > 32u) v |= static_cast<uint64_t>(regs_[i + 1]) << (32u - bit);
    return static_cast<uint32_t>(v & ((1ull << len) - 1ull));
}

Imx6IpuChannelDesc Imx6IpuCpmem::DecodeChannel(uint32_t ch) const {
    Imx6IpuChannelDesc d;
    d.eba0 = Field(ch, 1, 0, 29) << 3;
    d.eba1 = Field(ch, 1, 29, 29) << 3;
    d.eba = (ch < 64u && current_buffer_[ch]) ? d.eba1 : d.eba0;
    d.fw = Field(ch, 0, 125, 13) + 1u;
    d.fh = Field(ch, 0, 138, 12) + 1u;
    d.sl = Field(ch, 1, 102, 14) + 1u;
    d.bpp_code = Field(ch, 0, 107, 3);
    switch (d.bpp_code) {
    case 0: d.bits_per_pixel = 32u; break;
    case 1: d.bits_per_pixel = 24u; break;
    case 2: d.bits_per_pixel = 18u; break;
    case 3: d.bits_per_pixel = 16u; break;
    case 4: d.bits_per_pixel = 12u; break;
    case 5: d.bits_per_pixel = 8u; break;
    case 6: d.bits_per_pixel = 4u; break;
    default: emu_.Get<Fatal>().Die("[IPU CPMEM] channel %u uses unsupported BPP code %u", ch, d.bpp_code);
    }
    d.bytes_per_pixel = (d.bits_per_pixel + 7u) / 8u;
    d.pfs = Field(ch, 1, 85, 4);
    d.dec_sel = Field(ch, 0, 110, 2);
    d.separate_alpha = Field(ch, 1, 89, 1) != 0u;
    if (d.pfs != 5u) {
        for (uint32_t i = 0; i < d.component_width.size(); ++i) {
            d.component_width[i] = Field(ch, 1, 116u + i * 3u, 3) + 1u;
            d.component_offset[i] = Field(ch, 1, 128u + i * 5u, 5);
        }
    }
    const auto& ipu = emu_.Get<Imx6Ipu>();
    d.valid = ch < 64u && ipu.IdmacChannelEnabled(ch) && ipu.IdmacChannelReady(ch);
    return d;
}

REGISTER_SERVICE(Imx6IpuCpmem);

}
