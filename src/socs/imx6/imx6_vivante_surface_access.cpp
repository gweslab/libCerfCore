#include "imx6_vivante_surface_access.h"

#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace imx6_vivante {

namespace {

bool MapEndianBytes(uint64_t offset, uint32_t byte_count, uint32_t endian, uint64_t (&mapped)[4], uint64_t& first,
                    uint64_t& span) {
    first = UINT64_MAX;
    uint64_t last = 0u;
    for (uint32_t byte = 0u; byte < byte_count; ++byte) {
        mapped[byte] = VivanteBlitFormatOps::EndianByteOffset(offset + byte, endian);
        if (mapped[byte] < first) first = mapped[byte];
        if (mapped[byte] > last) last = mapped[byte];
    }
    span = last - first + 1u;
    return span <= 4u;
}

bool MapGpuByteSpan(uint32_t base, uint64_t offset, uint32_t byte_count, uint32_t endian, uint64_t (&mapped)[4],
                    uint64_t& first, uint64_t& span, uint32_t& address) {
    if (base == 0u || !MapEndianBytes(offset, byte_count, endian, mapped, first, span)) return false;
    const uint64_t gpu_address = static_cast<uint64_t>(base) + first;
    if (gpu_address > UINT32_MAX) return false;
    address = static_cast<uint32_t>(gpu_address);
    return true;
}

struct MappedGpuBytes {
    uint64_t mapped[4]{};
    uint64_t first = 0u;
    uint64_t span = 0u;
    uint32_t address = 0u;
    uint8_t bytes[4]{};
};

bool ReadGpuByteSpan(VivanteMem& mem, uint32_t base, uint64_t offset, uint32_t byte_count, uint32_t endian,
                     MmuClient client, MappedGpuBytes& result) {
    return MapGpuByteSpan(base, offset, byte_count, endian, result.mapped, result.first, result.span, result.address) &&
           mem.ReadGpuBytes(result.address, result.bytes, static_cast<size_t>(result.span), client);
}

struct LayoutAddress {
    uint32_t base = 0u;
    uint64_t offset = 0u;
};

bool ResolveLayoutAddress(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x, uint32_t y,
                          uint32_t byte_count, VivanteBlitSurfaceOps::SurfaceLayout layout,
                          LayoutAddress& result) {
    if (base == 0u) return false;
    const VivanteBlitSurfaceOps::SurfaceLocation location =
        VivanteBlitSurfaceOps::LocateSurface(stride, x, y, byte_count, layout);
    if (!location.valid) return false;
    result.base = location.plane ? extra_base : base;
    result.offset = location.offset;
    return result.base != 0u;
}

}

uint32_t VivanteSurfaceAccess::RequireMemoryDeFormat(uint32_t fmt, const char* operation,
                                                    uint32_t format_register) const {
    const uint32_t bpp = BppFromDeFormat(fmt);
    if (!IsKnownDeFormat(fmt) || bpp == 0u) {
        mem_.HaltUnsupported(operation, format_register, fmt);
    }
    return bpp;
}

bool VivanteSurfaceAccess::AddGpuOffset(uint32_t base, uint64_t offset, uint32_t& address) {
    if (static_cast<uint64_t>(base) + offset > UINT32_MAX) return false;
    address = base + static_cast<uint32_t>(offset);
    return true;
}

bool VivanteSurfaceAccess::ReadPackedGpu(uint32_t base, uint64_t offset, uint32_t byte_count, uint32_t endian,
                                         uint32_t& packed, MmuClient client) const {
    packed = 0u;
    MappedGpuBytes bytes{};
    if (!ReadGpuByteSpan(mem_, base, offset, byte_count, endian, client, bytes)) return false;
    uint8_t logical[4]{};
    for (uint32_t byte = 0u; byte < byte_count; ++byte)
        logical[byte] = bytes.bytes[bytes.mapped[byte] - bytes.first];
    if (byte_count == 1u) packed = logical[0];
    else if (byte_count == 2u) packed = cerf::le::U16(logical);
    else packed = cerf::le::U32(logical);
    return true;
}

bool VivanteSurfaceAccess::WritePackedGpu(uint32_t base, uint64_t offset, uint32_t byte_count, uint32_t endian,
                                          uint32_t packed, MmuClient client) const {
    MappedGpuBytes bytes{};
    if (!ReadGpuByteSpan(mem_, base, offset, byte_count, endian, client, bytes)) return false;
    for (uint32_t byte = 0u; byte < byte_count; ++byte) {
        bytes.bytes[bytes.mapped[byte] - bytes.first] = static_cast<uint8_t>(packed >> (byte * 8u));
    }
    return mem_.WriteGpuBytes(bytes.address, bytes.bytes, static_cast<size_t>(bytes.span), client);
}

bool VivanteSurfaceAccess::ReadLayoutPackedGpu(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x,
                                               uint32_t y, uint32_t byte_count, SurfaceLayout layout, uint32_t endian,
                                               uint32_t& packed, MmuClient client) const {
    LayoutAddress address{};
    if (!ResolveLayoutAddress(base, extra_base, stride, x, y, byte_count, layout, address)) return false;
    return ReadPackedGpu(address.base, address.offset, byte_count, endian, packed, client);
}

bool VivanteSurfaceAccess::WriteLayoutPackedGpu(uint32_t base, uint32_t stride, uint32_t x,
                                                uint32_t y, uint32_t byte_count, SurfaceLayout layout,
                                                uint32_t endian, uint32_t packed,
                                                MmuClient client) const {
    LayoutAddress address{};
    if (!ResolveLayoutAddress(base, 0u, stride, x, y, byte_count, layout, address)) return false;
    return WritePackedGpu(address.base, address.offset, byte_count, endian, packed, client);
}

bool VivanteSurfaceAccess::ReadSurfacePackedGpuLayout(uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t x,
                                                      uint32_t y, uint32_t fmt, uint32_t format_register,
                                                      SurfaceLayout layout,
                                                      uint32_t& packed, uint32_t endian,
                                                      MmuClient client) const {
    const uint32_t bpp = RequireMemoryDeFormat(fmt, "imx6-vivante unsupported surface-read format",
                                               format_register);
    return ReadLayoutPackedGpu(base, extra_base, stride, x, y, bpp, layout, endian, packed, client);
}

bool VivanteSurfaceAccess::ReadSurfacePixelGpuLayout(uint32_t base, uint32_t stride, uint32_t x,
                                                     uint32_t y, uint32_t fmt, uint32_t format_register,
                                                     uint32_t swizzle, SurfaceLayout layout,
                                                     uint32_t& argb, uint32_t endian,
                                                     MmuClient client) const {
    uint32_t packed = 0u;
    if (!ReadSurfacePackedGpuLayout(base, 0u, stride, x, y, fmt, format_register, layout, packed, endian,
                                    client)) {
        return false;
    }
    if ((fmt & 0x1Fu) == 9u)
        mem_.HaltUnsupported("imx6-vivante INDEX8 surface read without the index color table", base, fmt);
    argb = UnpackSurfaceColor(packed, fmt, swizzle);
    return true;
}

bool VivanteSurfaceAccess::WriteSurfacePixelGpuLayout(uint32_t base, uint32_t stride,
                                                      uint32_t x, uint32_t y, uint32_t fmt,
                                                      uint32_t format_register, uint32_t swizzle,
                                                      SurfaceLayout layout, uint32_t argb,
                                                      uint32_t endian, MmuClient client) const {
    if ((fmt & 0x1Fu) == 9u)
        mem_.HaltUnsupported("imx6-vivante INDEX8 destination format is unsupported", base, fmt);
    const uint32_t bpp = RequireMemoryDeFormat(fmt, "imx6-vivante unsupported surface-write format",
                                               format_register);
    const uint32_t packed = PackSurfaceColor(argb, fmt, swizzle);
    return WriteLayoutPackedGpu(base, stride, x, y, bpp, layout, endian, packed, client);
}

bool VivanteSurfaceAccess::ReadSurfacePixelGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt,
                                               uint32_t format_register, uint32_t swizzle, bool tiled, bool supertiled,
                                               uint32_t& argb, MmuClient client) const {
    const SurfaceLayout layout = DecodeSurfaceLayout(tiled, false, supertiled);
    return ReadSurfacePixelGpuLayout(base, stride, x, y, fmt, format_register, swizzle, layout, argb, 0u, client);
}

bool VivanteSurfaceAccess::WriteSurfacePixelGpu(uint32_t base, uint32_t stride, uint32_t x, uint32_t y, uint32_t fmt,
                                                uint32_t format_register, uint32_t swizzle, bool tiled, bool supertiled,
                                                uint32_t argb) const {
    const SurfaceLayout layout = DecodeSurfaceLayout(tiled, false, supertiled);
    return WriteSurfacePixelGpuLayout(base, stride, x, y, fmt, format_register, swizzle, layout, argb);
}

bool VivanteSurfaceAccess::ReadVrYuvPixelGpu(uint32_t y_address, uint32_t y_extra_address, uint32_t y_stride,
                                             uint32_t u_address, uint32_t u_stride, uint32_t v_address,
                                             uint32_t v_stride, uint32_t x, uint32_t y, uint32_t fmt,
                                             SurfaceLayout layout, uint32_t endian, bool uv_swizzle, bool bt709,
                                             uint32_t& argb) const {
    auto read_plane = [&](uint32_t base, uint32_t extra_base, uint32_t stride, uint32_t px, uint32_t py,
                          uint32_t bytes, SurfaceLayout plane_layout, uint32_t& value) -> bool {
        return ReadLayoutPackedGpu(base, extra_base, stride, px, py, bytes, plane_layout, endian, value,
                                   MmuClient::Texture);
    };

    uint32_t yy = 0u;
    uint32_t uu = 128u;
    uint32_t vv = 128u;
    const uint32_t format = fmt & 0x1Fu;
    if (format == 7u || format == 8u) {
        uint32_t even = 0u;
        uint32_t odd = 0u;
        const uint32_t pair_x = x & ~1u;
        if (!read_plane(y_address, y_extra_address, y_stride, pair_x, y, 2u, layout, even) ||
            !read_plane(y_address, y_extra_address, y_stride, pair_x + 1u, y, 2u, layout, odd)) {
            return false;
        }
        if (format == 7u) {
            yy = (x & 1u) ? (odd & 0xFFu) : (even & 0xFFu);
            uu = (even >> 8) & 0xFFu;
            vv = (odd >> 8) & 0xFFu;
        } else {
            yy = (x & 1u) ? ((odd >> 8) & 0xFFu) : ((even >> 8) & 0xFFu);
            uu = even & 0xFFu;
            vv = odd & 0xFFu;
        }
    } else {
        switch (format) {
    case 15u: {
        if (u_address == 0u || v_address == 0u || u_stride == 0u || v_stride == 0u) {
            return false;
        }
        if (!read_plane(y_address, 0u, y_stride, x, y, 1u, layout, yy) ||
            !read_plane(u_address, 0u, u_stride, x >> 1, y >> 1, 1u, layout, uu) ||
            !read_plane(v_address, 0u, v_stride, x >> 1, y >> 1, 1u, layout, vv)) {
            return false;
        }
        break;
    }
    case 17u:
    case 18u: {
        if (u_address == 0u || u_stride == 0u) return false;
        const uint32_t chroma_y = ((fmt & 0x1Fu) == 17u) ? (y >> 1) : y;
        uint32_t uv = 0u;
        if (!read_plane(y_address, 0u, y_stride, x, y, 1u, layout, yy) ||
            !read_plane(u_address, 0u, u_stride, x >> 1, chroma_y, 2u, layout, uv)) {
            return false;
        }
        uu = uv & 0xFFu;
        vv = (uv >> 8) & 0xFFu;
        break;
    }
    default: return false;
    }
    }
    if (uv_swizzle) {
        const uint32_t tmp = uu;
        uu = vv;
        vv = tmp;
    }
    argb = YuvToArgb(yy, uu, vv, bt709);
    return true;
}

}

}
