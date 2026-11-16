#include "imx6_vivante_draw2d_multisource.h"

#include "imx6_vivante_draw2d_regs.h"

namespace cerfcore {

namespace imx6_vivante {

VivanteDraw2dMultiSource::MultiSourceDesc VivanteDraw2dMultiSource::LoadMultiSource(uint32_t index) {
    MultiSourceDesc m{};
    const uint32_t off = index * 4u;
    m.address = mem_.StateReg(kD2dMultiSrcAddress + off);
    m.stride = mem_.StateReg(kD2dMultiSrcStride + off) & 0x3FFFFu;
    m.rot_config = mem_.StateReg(kD2dMultiSrcRotConfig + off);
    m.config = mem_.StateReg(kD2dMultiSrcConfig + off);
    if ((m.config & 0x20CFFF3Fu) != 0u)
        mem_.HaltUnsupported("kMultiSourceConfigFields", kD2dMultiSrcConfig + off, m.config);
    m.origin = mem_.StateReg(kD2dMultiSrcOrigin + off);
    m.size = mem_.StateReg(kD2dMultiSrcSize + off);
    m.color_key_low = mem_.StateReg(kD2dMultiSrcColorBg + off);
    m.color_key_high = mem_.StateReg(kD2dMultiSrcColorKeyHigh + off);
    m.rop = mem_.StateReg(kD2dMultiSrcRop + off);
    m.alpha_control = mem_.StateReg(kD2dMultiSrcAlphaControl + off);
    m.alpha_modes = mem_.StateReg(kD2dMultiSrcAlphaModes + off);
    m.rot_height = mem_.StateReg(kD2dMultiSrcRotHeight + off);
    m.rot_angle = mem_.StateReg(kD2dMultiSrcRotAngle + off);
    m.rotation_register = (mem_.UsesDeRotationAngle() ? kD2dMultiSrcRotAngle : kD2dMultiSrcRotConfig) + off;
    m.config_register = kD2dMultiSrcConfig + off;
    m.extent_register = kD2dMultiSrcRotConfig + off;
    m.global_src = mem_.StateReg(kD2dMultiSrcGlobalSrcColor + off);
    m.global_dst = mem_.StateReg(kD2dMultiSrcGlobalDstColor + off);
    m.color_multiply = mem_.StateReg(kD2dMultiSrcColorMultiply + off);
    m.transparency = mem_.StateReg(kD2dMultiSrcTransparency + off);
    const PeTransparencyConfig transparency = DecodeTransparency(m.transparency);
    m.source_transparency = transparency.source;
    m.pattern_transparency = transparency.pattern;
    m.destination_transparency = transparency.destination;
    m.use_source_override = transparency.source_override;
    m.use_pattern_override = transparency.pattern_override;
    m.use_destination_override = transparency.destination_override;
    if (m.source_transparency == 1u)
        mem_.HaltUnsupported("imx6-vivante multi-source source transparency kind is not modelled",
                             kD2dMultiSrcTransparency + off, m.transparency);
    if (!IsValidColorMultiplyModes(m.color_multiply))
        mem_.HaltUnsupported("imx6-vivante multi-source global premultiply encoding is not assigned",
                             kD2dMultiSrcColorMultiply + off, m.color_multiply);
    m.control = mem_.StateReg(kD2dMultiSrcControl + off);
    m.ex_config = mem_.StateReg(kD2dMultiSrcExConfig + off);
    m.extra_address = mem_.StateReg(kD2dMultiSrcExAddress + off);
    const DeSourceConfig source_config = DecodeDeSourceConfig(m.config, m.ex_config);
    m.format = source_config.format;
    m.swizzle = source_config.swizzle;
    m.endian = source_config.endian;
    if (!IsValidEndian(m.endian))
        mem_.HaltUnsupported("imx6-vivante multi-source ENDIAN_MODE encoding is not assigned",
                             kD2dMultiSrcConfig + off, m.endian);
    if (!IsModelledAlphaModes(m.alpha_modes))
        mem_.HaltUnsupported("imx6-vivante multi-source global alpha or blend mode is not modelled",
                             kD2dMultiSrcAlphaModes + off, m.alpha_modes);
    m.bpp = RequireMemoryDeFormat(m.format, "imx6-vivante unsupported multi-source format",
                                  kD2dMultiSrcConfig + off);
    m.relative = source_config.relative;
    m.tiled = source_config.tiled;
    m.supertiled = source_config.supertiled;
    if (source_config.minor_tiled)
        mem_.HaltUnsupported("imx6-vivante multi-source MINOR_TILED address permutation is not sourced",
                             kD2dMultiSrcExConfig + off, m.ex_config);
    m.layout = DecodeSurfaceLayout(m.tiled, source_config.multi_tiled, m.supertiled);
    const DeRotation source_rotation = DecodeDeRotation(m.rot_angle, m.rot_config, true, mem_.UsesDeRotationAngle());
    m.rotation = source_rotation.mode;
    m.mirror = source_rotation.mirror;
    if (!IsValidDeRot(m.rotation))
        mem_.HaltUnsupported("imx6-vivante multi-source DE_ROT_MODE encoding is not assigned", m.rotation_register, m.rotation);
    m.surface_w = m.rot_config & 0xFFFFu;
    m.surface_h = m.rot_height & 0xFFFFu;
    if (m.address && m.stride && m.bpp) {
        m.valid = mem_.TranslateGpuToHost(m.address, 1u) != nullptr &&
                  (!IsMultiLayout(m.layout) ||
                   (m.extra_address != 0u && mem_.TranslateGpuToHost(m.extra_address, 1u) != nullptr));
    }
    return m;
}

void VivanteDraw2dMultiSource::ReadMultiSource(const MultiSourceDesc& m, uint32_t x, uint32_t y, uint32_t& argb,
                                               uint32_t& packed) {
    const DeCoord p = VivanteBlitOps::TransformIfNeeded(x, y, m.surface_w, m.surface_h, m.rotation, m.mirror);
    if (!p.valid)
        mem_.HaltUnsupported("imx6-vivante multi-source rotation coordinate outside extent", m.extent_register,
                             (static_cast<uint64_t>(y) << 32) | x);
    if (!ReadSurfacePackedGpuLayout(m.address, m.extra_address, m.stride, p.x, p.y, m.format,
                                    m.config_register, m.layout, packed, m.endian))
        mem_.HaltUnsupported("imx6-vivante multi-source pixel read outside GPU-mapped memory", m.address,
                             (static_cast<uint64_t>(p.y) << 32) | p.x);
    const uint32_t index_color = m.format == 9u
                                     ? mem_.StateReg(kD2dIndexColorTable32 + (packed & 0xFFu) * 4u)
                                     : 0u;
    argb = DecodeSourcePixel(packed, m.format, m.swizzle, m.global_src, index_color);
}

void VivanteDraw2dMultiSource::ExecuteMultiSourceRect(uint32_t raw_x0, uint32_t raw_y0, uint32_t x0, uint32_t y0,
                                                      uint32_t w, uint32_t h) {
    const uint32_t control = mem_.StateReg(kD2dMultiSource);
    const uint32_t source_count = (control & 7u) + 1u;
    if (source_count > 4u)
        mem_.HaltUnsupported("imx6-vivante multi-source count exceeds four", kD2dMultiSource, control);

    MultiSourceDesc sources[4]{};
    for (uint32_t i = 0u; i < source_count; ++i) {
        sources[i] = LoadMultiSource(i);
        if (sources[i].control != 0u)
            mem_.HaltUnsupported("imx6-vivante multi-source CONTROL", kD2dMultiSrcControl + i * 4u,
                                 sources[i].control);
        if (!sources[i].valid)
            mem_.HaltUnsupported("imx6-vivante multi-source is not GPU-mapped", kD2dMultiSrcAddress + i * 4u,
                                 sources[i].address);
    }

    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            uint32_t current = 0u;
            uint32_t dst_packed = 0u;
            ReadDstActual(x0 + x, y0 + y, current, dst_packed);
            bool changed = false;
            for (uint32_t i = 0u; i < source_count; ++i) {
                const MultiSourceDesc& m = sources[i];
                const bool destination_opaque = DestinationAccepts(dst_packed, m.destination_transparency);
                const uint32_t dx = (x0 + x) - raw_x0;
                const uint32_t dy = (y0 + y) - raw_y0;
                uint32_t sx = 0u;
                uint32_t sy = 0u;
                if (!ResolveSourceCoordinate(Lo16(m.origin), m.relative, x0 + x, dx, sx) ||
                    !ResolveSourceCoordinate(Hi16(m.origin), m.relative, y0 + y, dy, sy))
                    mem_.HaltUnsupported(kNegativeRelativeSourceOrigin,
                                         kD2dMultiSrcOrigin + i * 4u, m.origin);
                uint32_t src_argb = 0u;
                uint32_t src_packed = 0u;
                ReadMultiSource(m, sx, sy, src_argb, src_packed);
                const bool source_transparent = VivanteBlitOps::SourceTransparent(
                    m.source_transparency, src_packed, m.color_key_low, m.color_key_high, m.format, m.swizzle);
                PatternOpaque(m.pattern_transparency);
                const bool transparent = VivanteBlitOps::IsTransparentPixel(destination_opaque, source_transparent);
                const bool alpha = (m.alpha_control & 1u) != 0u;
                if (SkipTransparentPixel(transparent, ((m.rop >> 20) & 3u) == 3u, alpha)) continue;

                if (!alpha) {
                    const bool foreground = !transparent;
                    const bool use_source = ResolveRopResource(RopBranchUsesSource(m.rop, foreground),
                                                               m.use_source_override);
                    const bool use_pattern = PatternUsed(m.rop, m.use_pattern_override, false, foreground);
                    const bool use_destination = ResolveRopResource(RopBranchUsesDestination(m.rop, foreground),
                                                                    m.use_destination_override);
                    const uint32_t pattern = use_pattern ? PatternPixel(x0 + x, y0 + y) : 0u;
                    const uint32_t rop_destination = ResolveRopInput(
                        m.rop, foreground, false, use_destination, use_destination ? current : 0u,
                        kD2dMultiSrcTransparency + i * 4u, m.transparency);
                    const uint32_t rop_source = ResolveRopInput(
                        m.rop, foreground, true, use_source, use_source ? src_argb : 0u,
                        kD2dMultiSrcTransparency + i * 4u, m.transparency);
                    current = BlendOrRop(false, m.rop, rop_destination, rop_source, pattern, foreground, m.alpha_modes,
                                         kD2dMultiSrcAlphaModes + i * 4u, m.color_multiply, m.global_src,
                                         m.global_dst);
                } else {
                    current = BlendOrRop(true, m.rop, current, src_argb, 0u, !transparent,
                                         m.alpha_modes, kD2dMultiSrcAlphaModes + i * 4u, m.color_multiply,
                                         m.global_src, m.global_dst);
                }
                changed = true;
            }
            if (changed) WriteDst(x0 + x, y0 + y, current);
        }
    }
}

}

}
