#include "imx6_vivante_draw2d_state.h"

#include "imx6_vivante_draw2d_regs.h"

#include <algorithm>
#include <cstring>

namespace cerfcore {

namespace imx6_vivante {

namespace {

constexpr char kPatternBitOrder[] = "kPatternBitOrder";
constexpr char kSourceMaskTransparency[] = "kSourceMaskTransparency";
constexpr uint32_t kD2dPatternConfigUnmodelledMask = 0x2088FF00u;

}

void ValidateDeConfiguration(VivanteMem& mem, uint32_t src_cfg, uint32_t src_ex_cfg, uint32_t dst_cfg,
                             uint32_t raw_dst_stride) {
    if ((raw_dst_stride & 0xFFFC0000u) != 0u)
        mem.HaltUnsupported("imx6-vivante destination supertile or multi-tile stride is not guest-programmed",
                            kD2dDestStride, raw_dst_stride);
    const bool mono_source = ((src_cfg >> 24) & 0x1Fu) == 10u;
    const uint32_t src_cfg_unmodelled = mono_source ? 0x20CE4E30u : 0x20CF4E30u;
    if ((src_cfg & src_cfg_unmodelled) != 0u)
        mem.HaltUnsupported("kSourceConfigFields", kD2dSrcConfig, src_cfg);
    if ((dst_cfg & 0x780C0EE0u) != 0u)
        mem.HaltUnsupported("kDestinationConfigReservedBits", kD2dDestConfig, dst_cfg);
    if ((dst_cfg & (1u << 25)) != 0u)
        mem.HaltUnsupported("kInterTilePerformanceFix", kD2dDestConfig, dst_cfg);
    const VivanteBlitOps::DeSourceConfig src = VivanteBlitOps::DecodeDeSourceConfig(src_cfg, src_ex_cfg);
    const VivanteBlitOps::DeDestinationConfig dst = VivanteBlitOps::DecodeDeDestinationConfig(dst_cfg);
    if (!VivanteBlitOps::IsValidEndian(src.endian))
        mem.HaltUnsupported("imx6-vivante source ENDIAN_MODE encoding is not assigned", kD2dSrcConfig,
                            src.endian);
    if (!VivanteBlitOps::IsValidEndian(dst.endian))
        mem.HaltUnsupported("imx6-vivante destination ENDIAN_MODE encoding is not assigned", kD2dDestConfig,
                            dst.endian);
    if (src.minor_tiled)
        mem.HaltUnsupported("imx6-vivante source MINOR_TILED address permutation is not sourced",
                            kD2dSrcExConfig, src_ex_cfg);
    if (dst.minor_tiled)
        mem.HaltUnsupported("imx6-vivante destination MINOR_TILED address permutation is not sourced",
                            kD2dDestConfig, dst_cfg);
}

void VivanteDraw2dState::LoadState() {
    dst_addr_ = mem_.StateReg(kD2dDestAddress);
    const uint32_t raw_dst_stride = mem_.StateReg(kD2dDestStride);
    src_cfg_ = mem_.StateReg(kD2dSrcConfig);
    src_ex_cfg_ = mem_.StateReg(kD2dSrcExConfig);
    dst_cfg_ = mem_.StateReg(kD2dDestConfig);
    ValidateDeConfiguration(mem_, src_cfg_, src_ex_cfg_, dst_cfg_, raw_dst_stride);
    dst_stride_ = raw_dst_stride & 0x3FFFFu;
    gdi_stretch_ = (dst_cfg_ & (1u << 24)) != 0u;
    const DeDestinationConfig dst_config = DecodeDeDestinationConfig(dst_cfg_);
    dst_fmt_ = dst_config.format;
    dst_tiled_ = dst_config.tiled;
    dst_layout_ = DecodeSurfaceLayout(dst_tiled_, false, false);
    dst_cmd_ = (dst_cfg_ >> 12) & 0xFu;
    dst_swizzle_ = dst_config.swizzle;
    dst_endian_ = dst_config.endian;
    dst_bpp_ = RequireMemoryDeFormat(dst_fmt_, "imx6-vivante unsupported DRAW_2D destination format",
                                     kD2dDestConfig);
    if (dst_fmt_ == 9u)
        mem_.HaltUnsupported("imx6-vivante INDEX8 DRAW_2D destination", kD2dDestConfig, dst_cfg_);
    dst_ptr_ = mem_.TranslateGpuToHostWrite(dst_addr_, 1u);

    dst_ready_ = dst_ptr_ && dst_stride_ != 0u;

    src_addr_ = mem_.StateReg(kD2dSrcAddress);
    src_stride_ = mem_.StateReg(kD2dSrcStride) & 0x3FFFFu;
    src_ex_addr_ = mem_.StateReg(kD2dSrcExAddress);
    const DeSourceConfig src_config = DecodeDeSourceConfig(src_cfg_, src_ex_cfg_);
    src_fmt_ = src_config.format;
    src_swizzle_ = src_config.swizzle;
    src_endian_ = src_config.endian;
    src_relative_ = src_config.relative;
    src_tiled_ = src_config.tiled;
    src_multi_tiled_ = src_config.multi_tiled;
    src_supertiled_ = src_config.supertiled;
    src_layout_ = DecodeSurfaceLayout(src_tiled_, src_multi_tiled_, src_supertiled_);
    src_stream_ = (src_cfg_ & (1u << 8)) != 0u;
    src_pack_ = (src_cfg_ >> 12) & 3u;
    mono_transparent_one_ = (src_cfg_ & (1u << 15)) != 0u;
    src_bpp_ = BppFromDeFormat(src_fmt_);
    if (!IsKnownDeFormat(src_fmt_) || (src_bpp_ == 0u && !(src_fmt_ == 10u && src_stream_))) {
        mem_.HaltUnsupported("imx6-vivante unsupported DRAW_2D source format", kD2dSrcConfig, src_fmt_);
    }
    src_ptr_ = (src_addr_ && src_stride_ && src_bpp_) ? mem_.TranslateGpuToHost(src_addr_, 1u) : nullptr;
    src_extra_ptr_ = IsMultiLayout(src_layout_) && src_ex_addr_ ? mem_.TranslateGpuToHost(src_ex_addr_, 1u) : nullptr;
    src_surface_configured_ = src_addr_ != 0u && src_stride_ != 0u && src_bpp_ != 0u &&
                              (!IsMultiLayout(src_layout_) || src_ex_addr_ != 0u);

    src_origin_ = mem_.StateReg(kD2dSrcOrigin);
    src_size_ = mem_.StateReg(kD2dSrcSize);
    src_x0_ = Lo16(src_origin_);
    src_y0_ = Hi16(src_origin_);
    src_size_x_ = Lo16(src_size_);
    src_size_y_ = Hi16(src_size_);
    stretch_x_ = mem_.StateReg(kD2dStretchX) & 0x7FFFFFFFu;
    stretch_y_ = mem_.StateReg(kD2dStretchY) & 0x7FFFFFFFu;
    if (dst_cmd_ == 4u && (stretch_x_ == 0u || stretch_y_ == 0u))
        mem_.HaltUnsupported("imx6-vivante stretch factor was not programmed by the guest", kD2dStretchX,
                             (static_cast<uint64_t>(stretch_y_) << 32) | stretch_x_);

    clear_argb_ = mem_.StateReg(kD2dClearPe20);

    src_bg_ = mem_.StateReg(kD2dSrcColorBg);
    src_fg_ = mem_.StateReg(kD2dSrcColorFg);
    pat_fg_ = mem_.StateReg(kD2dPatternFg);
    const uint32_t clip_low = mem_.StateReg(kD2dDestClipLow);
    const uint32_t clip_high = mem_.StateReg(kD2dDestClipHigh);
    clip_x0_ = Lo16(clip_low) & 0x7FFFu;
    clip_y0_ = Hi16(clip_low) & 0x7FFFu;
    clip_x1_ = Lo16(clip_high) & 0x7FFFu;
    clip_y1_ = Hi16(clip_high) & 0x7FFFu;
    rop_ = mem_.StateReg(kD2dRop);
    alpha_control_ = mem_.StateReg(kD2dAlphaControl);
    alpha_enable_ = (alpha_control_ & 1u) != 0u;
    alpha_modes_ = mem_.StateReg(kD2dAlphaModes);
    if (!VivanteBlitOps::IsModelledAlphaModes(alpha_modes_))
        mem_.HaltUnsupported("imx6-vivante global alpha or blend mode is not modelled", kD2dAlphaModes,
                             alpha_modes_);
    global_src_ = mem_.StateReg(kD2dGlobalSrcColor);
    global_dst_ = mem_.StateReg(kD2dGlobalDstColor);
    color_multiply_modes_ = mem_.StateReg(kD2dColorMultiplyModes);
    if (!VivanteBlitOps::IsValidColorMultiplyModes(color_multiply_modes_))
        mem_.HaltUnsupported("imx6-vivante global premultiply encoding is not assigned", kD2dColorMultiplyModes,
                             color_multiply_modes_);
    const uint32_t pe_transparency = mem_.StateReg(kD2dPeTransparency);
    const PeTransparencyConfig transparency = DecodeTransparency(pe_transparency);
    pe_src_transparency_ = transparency.source;
    pe_pat_transparency_ = transparency.pattern;
    pe_dst_transparency_ = transparency.destination;
    use_src_override_ = transparency.source_override;
    use_pat_override_ = transparency.pattern_override;
    use_dst_override_ = transparency.destination_override;
    rop_uses_source_ = RopUsesSource(rop_);
    rop_uses_destination_ = RopUsesDestination(rop_);
    use_source_ = ResolveRopResource(rop_uses_source_, use_src_override_);
    use_destination_ = (alpha_enable_ || pe_dst_transparency_ == 2u)
                           ? true
                           : ResolveRopResource(rop_uses_destination_, use_dst_override_);
    pat_addr_ = mem_.StateReg(kD2dPatternAddress);
    pat_cfg_ = mem_.StateReg(kD2dPatternConfig);
    if ((pat_cfg_ & kD2dPatternConfigUnmodelledMask) != 0u)
        mem_.HaltUnsupported("imx6-vivante PATTERN_CONFIG unassigned bits", kD2dPatternConfig, pat_cfg_);
    pat_low_ = mem_.StateReg(kD2dPatternLow);
    pat_high_ = mem_.StateReg(kD2dPatternHigh);
    pat_mask_low_ = mem_.StateReg(kD2dPatternMaskLow);
    pat_mask_high_ = mem_.StateReg(kD2dPatternMaskHigh);
    if (s_.pattern_init_valid_) {
        const bool different = s_.pattern_init_config_ != (pat_cfg_ & ~0xC0u) ||
                               s_.pattern_init_address_ != pat_addr_ ||
                               s_.pattern_init_low_ != pat_low_ ||
                               s_.pattern_init_high_ != pat_high_ ||
                               s_.pattern_init_mask_low_ != pat_mask_low_ ||
                               s_.pattern_init_mask_high_ != pat_mask_high_;
        if (different)
            mem_.HaltUnsupported("PATTERN_CONFIG INIT_TRIGGER latch vs draw-time fetch differ: no source decides",
                                 kD2dPatternConfig, pat_cfg_);
        if (s_.pattern_init_byte_count_ != 0u) {
            uint8_t draw_bytes[256]{};
            if (!mem_.ReadGpuBytes(pat_addr_, draw_bytes, s_.pattern_init_byte_count_) ||
                std::memcmp(draw_bytes, s_.pattern_init_bytes_, s_.pattern_init_byte_count_) != 0)
                mem_.HaltUnsupported("PATTERN_CONFIG INIT_TRIGGER latch vs draw-time fetch differ: no source decides",
                                     kD2dPatternAddress, pat_addr_);
        }
    }
    pat_fmt_ = pat_cfg_ & 0xFu;
    pat_extended_fmt_ = (pat_cfg_ >> 24) & 0x1Fu;
    pat_origin_x_ = PatternOriginX(pat_cfg_);
    pat_origin_y_ = PatternOriginY(pat_cfg_);
    if ((pat_extended_fmt_ & 0xFu) != pat_fmt_)
        mem_.HaltUnsupported("imx6-vivante PATTERN_CONFIG format fields disagree", kD2dPatternConfig, pat_cfg_);
    pat_memory_ = (pat_cfg_ & (1u << 4)) != 0u;
    pat_bpp_ = VivanteBlitOps::PatternBytesPerPixel(mem_, pat_cfg_);
    if (pat_memory_ && (!IsKnownDeFormat(pat_extended_fmt_) || (pat_bpp_ == 0u && pat_extended_fmt_ != 10u))) {
        mem_.HaltUnsupported("imx6-vivante unsupported DRAW_2D pattern format", kD2dPatternConfig,
                             pat_extended_fmt_);
    }
    pat_ptr_ = (pat_memory_ && pat_addr_ && pat_bpp_) ? mem_.TranslateGpuToHost(pat_addr_, 1u) : nullptr;
    src_key_low_ = src_bg_;
    src_key_high_ = mem_.StateReg(kD2dSrcColorKeyHigh);
    dst_key_low_ = mem_.StateReg(kD2dDestColorKey);
    dst_key_high_ = mem_.StateReg(kD2dDestColorKeyHigh);

    src_rot_cfg_ = mem_.StateReg(kD2dSrcRotConfig);
    dst_rot_cfg_ = mem_.StateReg(kD2dDestRotConfig);
    rot_angle_ = mem_.StateReg(kD2dRotAngle);
    const bool angle_supported = mem_.UsesDeRotationAngle();
    const DeRotation src_rotation = DecodeDeRotation(rot_angle_, src_rot_cfg_, true, angle_supported);
    const DeRotation dst_rotation = DecodeDeRotation(rot_angle_, dst_rot_cfg_, false, angle_supported);
    src_rot_ = src_rotation.mode;
    dst_rot_ = dst_rotation.mode;
    if (!VivanteBlitOps::IsValidDeRot(src_rot_))
        mem_.HaltUnsupported("imx6-vivante source DE_ROT_MODE encoding is not assigned",
                             angle_supported ? kD2dRotAngle : kD2dSrcRotConfig, src_rot_);
    if (!VivanteBlitOps::IsValidDeRot(dst_rot_))
        mem_.HaltUnsupported("imx6-vivante destination DE_ROT_MODE encoding is not assigned",
                             angle_supported ? kD2dRotAngle : kD2dDestRotConfig, dst_rot_);
    src_mirror_ = src_rotation.mirror;
    dst_mirror_ = dst_rotation.mirror;
    src_surface_w_ = src_rot_cfg_ & 0xFFFFu;
    src_surface_h_ = mem_.StateReg(kD2dSrcRotHeight) & 0xFFFFu;
    dst_surface_w_ = dst_rot_cfg_ & 0xFFFFu;
    dst_surface_h_ = mem_.StateReg(kD2dDestRotHeight) & 0xFFFFu;

    dst_mirror_rect_valid_ = false;
}

VivanteDraw2dState::DeCoord VivanteDraw2dState::DstCoord(uint32_t x, uint32_t y) const {
    if (dst_rot_ == 0u && dst_mirror_ == 0u) return {x, y};
    if (dst_rot_ == 0u && dst_mirror_rect_valid_) {
        if (x >= dst_surface_w_ || y >= dst_surface_h_)
            mem_.HaltUnsupported("imx6-vivante destination mirror coordinate outside rotation extent",
                                 kD2dDestRotConfig, (static_cast<uint64_t>(y) << 32) | x);
        DeCoord p{x, y};
        if ((dst_mirror_ & 1u) != 0u && p.x >= dst_mirror_rect_x0_ && p.x < dst_mirror_rect_x1_) {
            p.x = dst_mirror_rect_x0_ + (dst_mirror_rect_x1_ - 1u - p.x);
        }
        if ((dst_mirror_ & 2u) != 0u && p.y >= dst_mirror_rect_y0_ && p.y < dst_mirror_rect_y1_) {
            p.y = dst_mirror_rect_y0_ + (dst_mirror_rect_y1_ - 1u - p.y);
        }
        return p;
    }
    const DeCoord p = VivanteBlitOps::TransformIfNeeded(x, y, dst_surface_w_, dst_surface_h_, dst_rot_, dst_mirror_);
    if (!p.valid)
        mem_.HaltUnsupported("imx6-vivante destination rotation coordinate outside extent", kD2dDestRotConfig,
                             (static_cast<uint64_t>(y) << 32) | x);
    return p;
}

void VivanteDraw2dState::ReadDstActual(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed) {
    const DeCoord p = DstCoord(x, y);
    if (!ReadSurfacePackedGpuLayout(dst_addr_, 0u, dst_stride_, p.x, p.y, dst_fmt_, kD2dDestConfig,
                                    dst_layout_, packed, dst_endian_, MmuClient::PixelEngine))
        mem_.HaltUnsupported("imx6-vivante destination pixel read outside GPU-mapped memory", dst_addr_,
                             (static_cast<uint64_t>(p.y) << 32) | p.x);
    argb = UnpackSurfaceColor(packed, dst_fmt_, dst_swizzle_);
}

void VivanteDraw2dState::ReadDst(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed) {
    if (!use_destination_) {
        argb = 0u;
        packed = 0u;
        return;
    }
    ReadDstActual(x, y, argb, packed);
}

void VivanteDraw2dState::WriteDst(uint32_t x, uint32_t y, uint32_t argb) {
    const DeCoord p = DstCoord(x, y);
    if (!WriteSurfacePixelGpuLayout(dst_addr_, dst_stride_, p.x, p.y, dst_fmt_, kD2dDestConfig, dst_swizzle_,
                                    dst_layout_, argb, dst_endian_))
        mem_.HaltUnsupported("imx6-vivante destination pixel write outside GPU-mapped memory", dst_addr_,
                             (static_cast<uint64_t>(p.y) << 32) | p.x);
}

bool VivanteDraw2dState::FastSolidPatternFill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    if (dst_layout_ != SurfaceLayout::Linear || dst_bpp_ != 4u || dst_endian_ != 0u || dst_cmd_ != 2u ||
        alpha_enable_ || use_destination_ || pe_pat_transparency_ == 1u ||
        (pat_cfg_ & (1u << 4)) || dst_rot_ != 0u || (dst_mirror_ & 1u) != 0u)
        return false;

    const uint32_t pat_argb = PatternPixel(x0, y0);
    const uint32_t dst_argb = ResolveRopInput(rop_, true, false, use_destination_, 0u,
                                              kD2dPeTransparency, mem_.StateReg(kD2dPeTransparency));
    const uint32_t src_argb = ResolveRopInput(rop_, true, true, false, 0u,
                                              kD2dPeTransparency, mem_.StateReg(kD2dPeTransparency));
    const uint32_t out_argb = ApplyRop(rop_, dst_argb, src_argb, pat_argb);
    const uint32_t packed = PackSurfaceColor(out_argb, dst_fmt_, dst_swizzle_);
    solid_fill_row_.resize(w);
    std::fill(solid_fill_row_.begin(), solid_fill_row_.end(), packed);
    for (uint32_t y = 0u; y < h; ++y) {
        const DeCoord p = DstCoord(x0, y0 + y);
        const SurfaceLocation location = LocateSurface(dst_stride_, p.x, p.y, dst_bpp_, SurfaceLayout::Linear);
        const uint64_t row_bytes = static_cast<uint64_t>(w) * dst_bpp_;
        uint32_t address = 0u;
        if (!location.valid || !AddGpuOffset(dst_addr_, location.offset, address) ||
            !mem_.WriteGpuBytes(address, solid_fill_row_.data(), static_cast<size_t>(row_bytes),
                                MmuClient::PixelEngine))
            return false;
    }
    return true;
}

void VivanteDraw2dState::ClearDst(uint32_t x, uint32_t y) {
    WriteDst(x, y, clear_argb_);
}

bool VivanteDraw2dState::SourceTransparent(uint32_t packed) const {
    if (pe_src_transparency_ == 1u)
        mem_.HaltUnsupported(kSourceMaskTransparency, kD2dPeTransparency, pe_src_transparency_);
    return VivanteBlitOps::SourceTransparent(pe_src_transparency_, packed, src_key_low_, src_key_high_, src_fmt_,
                                             src_swizzle_);
}

bool VivanteDraw2dState::PatternUsed(uint32_t rop, uint32_t override_mode, bool both_branches,
                                     bool foreground) const {
    const bool foreground_uses = RopBranchUsesPattern(rop, foreground);
    const bool background_uses = both_branches && RopBranchUsesPattern(rop, !foreground);
    return ResolveRopResource(foreground_uses || background_uses, override_mode);
}

bool VivanteDraw2dState::SkipTransparentPixel(bool transparent, bool type3_rop, bool blend) const {
    return transparent && (!type3_rop || blend);
}

uint32_t VivanteDraw2dState::BlendOrRop(bool blend, uint32_t rop, uint32_t dst, uint32_t src,
                                        uint32_t pat, bool foreground, uint32_t alpha_modes,
                                        uint32_t alpha_modes_register,
                                        uint32_t color_multiply_modes, uint32_t global_src,
                                        uint32_t global_dst) const {
    return blend ? BlendPePixel(mem_, src, dst, alpha_modes, alpha_modes_register,
                                color_multiply_modes, global_src, global_dst)
                 : ApplyRop(rop, dst, src, pat, foreground);
}

uint32_t VivanteDraw2dState::ResolveRopInput(uint32_t rop, bool foreground, bool source, bool fetched,
                                             uint32_t value, uint32_t register_offset,
                                             uint32_t register_value) const {
    const bool required = source ? RopBranchUsesSource(rop, foreground)
                                 : RopBranchUsesDestination(rop, foreground);
    if (required && !fetched)
        mem_.HaltUnsupported(kRopInputNotFetched, register_offset, register_value);
    return required ? value : 0u;
}

VivanteBlitOps::PeTransparencyConfig VivanteDraw2dState::DecodeTransparency(uint32_t value) const {
    if ((value & 0x20000000u) != 0u)
        mem_.HaltUnsupported("imx6-vivante PE_TRANSPARENCY DFB_COLOR_KEY is not modelled",
                             kD2dPeTransparency, value);
    if (!VivanteBlitOps::IsValidPeTransparency(value))
        mem_.HaltUnsupported("imx6-vivante PE_TRANSPARENCY encoding is not assigned",
                             kD2dPeTransparency, value);
    return DecodePeTransparency(value);
}

bool VivanteDraw2dState::DestinationAccepts(uint32_t packed) const {
    return DestinationAccepts(packed, pe_dst_transparency_);
}

bool VivanteDraw2dState::DestinationAccepts(uint32_t packed, uint32_t transparency_kind) const {
    if (transparency_kind != 2u) return true;
    return NativeColorKeyMatch(packed, dst_key_low_, dst_key_high_, dst_fmt_, dst_swizzle_);
}

void VivanteDraw2dState::PatternOpaque() const {
    PatternOpaque(pe_pat_transparency_);
}

void VivanteDraw2dState::PatternOpaque(uint32_t transparency_kind) const {
    if (transparency_kind != 1u) return;
    if (pat_mask_low_ != 0xFFFFFFFFu || pat_mask_high_ != 0xFFFFFFFFu)
        mem_.HaltUnsupported(kPatternBitOrder, kD2dPatternMaskLow,
                             (static_cast<uint64_t>(pat_mask_high_) << 32) | pat_mask_low_);
}


bool VivanteDraw2dState::ReadPatternMemory(uint32_t x, uint32_t y, uint32_t& argb) {
    if (!pat_ptr_) return false;

    const uint32_t px = (x - pat_origin_x_) & 7u;
    const uint32_t py = (y - pat_origin_y_) & 7u;

    if (pat_extended_fmt_ == 9u) {
        uint32_t ix = 0u;
        if (!ReadPackedGpu(pat_addr_, py * 8u + px, 1u, 0u, ix)) {
            mem_.HaltUnsupported("imx6-vivante pattern pixel read outside GPU-mapped memory", pat_addr_,
                                 (static_cast<uint64_t>(py) << 32) | px);
        }
        argb = mem_.StateReg(kD2dIndexColorTable32 + (ix & 0xFFu) * 4u);
        return true;
    }

    if (!ReadSurfacePixelGpu(pat_addr_, 8u * pat_bpp_, px, py, pat_extended_fmt_, kD2dPatternConfig, 0u,
                             false, false, argb))
        mem_.HaltUnsupported("imx6-vivante pattern pixel read outside GPU-mapped memory", pat_addr_,
                             (static_cast<uint64_t>(py) << 32) | px);
    return true;
}

uint32_t VivanteDraw2dState::PatternPixel(uint32_t x, uint32_t y) {
    uint32_t argb = 0u;
    if (ReadPatternMemory(x, y, argb)) return argb;
    if (pat_memory_ && pat_extended_fmt_ != 10u)
        mem_.HaltUnsupported("imx6-vivante memory pattern is unavailable", kD2dPatternConfig, pat_cfg_);
    if ((pat_cfg_ & (1u << 4)) != 0u)
        mem_.HaltUnsupported(kPatternBitOrder, kD2dPatternLow,
                             (static_cast<uint64_t>(pat_high_) << 32) | pat_low_);
    return (pat_cfg_ & (1u << 5)) != 0u ? VivanteBlitOps::UnpackSurfaceColor(pat_fg_, pat_extended_fmt_) : pat_fg_;
}

VivanteDraw2dState::DeCoord VivanteDraw2dState::SrcCoord(uint32_t x, uint32_t y) const {
    const DeCoord p = VivanteBlitOps::TransformIfNeeded(x, y, src_surface_w_, src_surface_h_, src_rot_, src_mirror_);
    if (!p.valid)
        mem_.HaltUnsupported("imx6-vivante source rotation coordinate outside extent", kD2dSrcRotConfig,
                             (static_cast<uint64_t>(y) << 32) | x);
    return p;
}

void VivanteDraw2dState::ReadSource(uint32_t x, uint32_t y, uint32_t& argb, uint32_t& packed) {
    const DeCoord sp = SrcCoord(x, y);
    if (!ReadSurfacePackedGpuLayout(src_addr_, src_ex_addr_, src_stride_, sp.x, sp.y, src_fmt_, kD2dSrcConfig,
                                    src_layout_, packed, src_endian_))
        mem_.HaltUnsupported("imx6-vivante source pixel read outside GPU-mapped memory", src_addr_,
                             (static_cast<uint64_t>(sp.y) << 32) | sp.x);

    const uint32_t index_color = (src_fmt_ & 0x1Fu) == 9u
                                     ? mem_.StateReg(kD2dIndexColorTable32 + (packed & 0xFFu) * 4u)
                                     : 0u;
    argb = DecodeSourcePixel(packed, src_fmt_, src_swizzle_, global_src_, index_color);
}

}

}
