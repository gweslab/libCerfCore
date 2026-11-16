#include "imx6_vivante_draw2d.h"
#include "imx6_vivante_draw2d_regs.h"

namespace cerfcore {

namespace imx6_vivante {

void VivanteDraw2d::Execute(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data,
                            uint32_t stream_words) {
    LoadState();
    if (!dst_ready_)
        mem_.HaltUnsupported("imx6-vivante DRAW_2D destination unmapped or with zero stride", dst_addr_, dst_stride_);

    const uint8_t* stream = stream_words ? reinterpret_cast<const uint8_t*>(stream_data) : nullptr;

    if (src_stream_ && rect_count > 1u)
        mem_.HaltUnsupported("imx6-vivante DRAW_2D stream source with several rectangles", kD2dSrcConfig,
                             rect_count);
    for (uint32_t i = 0; i < rect_count; ++i) {
        dst_mirror_rect_valid_ = false;
        const uint32_t rw[2] = {
            rect_words[i * 2u],
            rect_words[i * 2u + 1u],
        };
        uint32_t x0 = Lo16(rw[0]), y0 = Hi16(rw[0]);
        uint32_t x1 = Lo16(rw[1]), y1 = Hi16(rw[1]);
        const uint32_t rect_x0 = x0, rect_y0 = y0;
        const uint32_t rect_x1 = x1, rect_y1 = y1;

        if (dst_cmd_ == 1u) {
            ExecuteLine(rect_x0, rect_y0, rect_x1, rect_y1);
            continue;
        }

        const uint32_t rect_w = (rect_x1 > rect_x0) ? (rect_x1 - rect_x0) : 0u;
        const uint32_t rect_h = (rect_y1 > rect_y0) ? (rect_y1 - rect_y0) : 0u;
        if (rect_w != 0u && rect_h != 0u) {
            dst_mirror_rect_x0_ = rect_x0;
            dst_mirror_rect_y0_ = rect_y0;
            dst_mirror_rect_x1_ = rect_x1;
            dst_mirror_rect_y1_ = rect_y1;
            dst_mirror_rect_valid_ = true;
        }
        x0 = (x0 > clip_x0_) ? x0 : clip_x0_;
        y0 = (y0 > clip_y0_) ? y0 : clip_y0_;
        x1 = (x1 < clip_x1_) ? x1 : clip_x1_;
        y1 = (y1 < clip_y1_) ? y1 : clip_y1_;
        if (clip_x1_ <= clip_x0_ || clip_y1_ <= clip_y0_ || x1 <= x0 || y1 <= y0) continue;
        const uint32_t w = x1 - x0;
        const uint32_t h = y1 - y0;

        if (dst_cmd_ == 0u) {
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                    ClearDst(x0 + x, y0 + y);
            continue;
        }

        if (dst_cmd_ == 8u) {
            ExecuteMultiSourceRect(rect_x0, rect_y0, x0, y0, w, h);
            continue;
        }

        if ((dst_cmd_ == 2u || dst_cmd_ == 3u || dst_cmd_ == 4u) && src_stream_ && stream) {
            ExecuteMonoStream(stream, stream_words, rect_x0, rect_y0, rect_w, rect_h, x0, y0, w, h);
            continue;
        }

        if ((dst_cmd_ == 2u || dst_cmd_ == 3u || dst_cmd_ == 4u) && use_source_) {
            if (!src_surface_configured_)
                mem_.HaltUnsupported("imx6-vivante DRAW_2D source operand without a configured source surface",
                                     src_addr_, src_cfg_);
            ExecuteCopyLike(rect_x0, rect_y0, x0, y0, w, h);
            continue;
        }

        if (dst_cmd_ == 2u || dst_cmd_ == 3u || dst_cmd_ == 4u) {
            ExecutePatternFill(x0, y0, w, h);
            continue;
        }

        mem_.HaltUnsupported("imx6-vivante DRAW_2D command is not modelled", kD2dDestConfig, dst_cmd_);
    }
}

void VivanteDraw2d::ExecuteLine(uint32_t rect_x0, uint32_t rect_y0, uint32_t rect_x1, uint32_t rect_y1) {
    if (alpha_enable_)
        mem_.HaltUnsupported("imx6-vivante line alpha blending is unsupported", kD2dAlphaControl,
                             alpha_control_);
    if (pe_src_transparency_ != 0u)
        mem_.HaltUnsupported("imx6-vivante line source transparency is unsupported", kD2dPeTransparency,
                             mem_.StateReg(kD2dPeTransparency));
    const bool use_pattern = PatternUsed(rop_, use_pat_override_, true, true);
    RasterizeLineBresenhamExclusive(rect_x0, rect_y0, rect_x1, rect_y1, [&](uint32_t px, uint32_t py) {
        if (clip_x1_ <= clip_x0_ || clip_y1_ <= clip_y0_ || px < clip_x0_ || px >= clip_x1_ || py < clip_y0_ ||
            py >= clip_y1_)
            return;

        ExecutePixel(px, py, src_fg_, nullptr, use_pattern, true);
    });
}

bool VivanteDraw2d::IsType3Rop() const { return ((rop_ >> 20) & 3u) == 3u; }

uint32_t VivanteDraw2d::StretchSourceOffset(uint32_t destination, uint32_t origin,
                                            uint32_t programmed_factor) const {
    const uint32_t offset = destination - origin;
    return dst_cmd_ == 4u ? StretchCoordinate(offset, programmed_factor, gdi_stretch_)
                          : offset;
}

void VivanteDraw2d::ExecutePixel(uint32_t x, uint32_t y, uint32_t src_argb, const uint32_t* src_packed,
                                bool use_pattern, bool source_valid) {
    if (alpha_enable_ && !source_valid)
        mem_.HaltUnsupported(kAlphaBlendSourceWithoutSurface, kD2dAlphaControl,
                             mem_.StateReg(kD2dAlphaControl));
    uint32_t dst_argb = 0u;
    uint32_t dst_packed = 0u;
    ReadDst(x, y, dst_argb, dst_packed);
    const bool destination_opaque = DestinationAccepts(dst_packed);
    const bool source_transparent = source_valid && src_packed && SourceTransparent(*src_packed);
    if (use_pattern) PatternOpaque();
    const bool type3_rop = IsType3Rop();
    const bool transparent = VivanteBlitOps::IsTransparentPixel(destination_opaque, source_transparent);
    if (SkipTransparentPixel(transparent, type3_rop, alpha_enable_)) return;
    const uint32_t pat_argb = use_pattern ? PatternPixel(x, y) : 0u;
    const bool foreground = !transparent;
    const uint32_t pixel_src = alpha_enable_ ? src_argb
        : ResolveRopInput(rop_, foreground, true, source_valid, src_argb,
                          kD2dPeTransparency, mem_.StateReg(kD2dPeTransparency));
    if (!alpha_enable_)
        dst_argb = ResolveRopInput(rop_, foreground, false, use_destination_, dst_argb,
                                   kD2dPeTransparency, mem_.StateReg(kD2dPeTransparency));
    const uint32_t out_argb = BlendOrRop(alpha_enable_, rop_, dst_argb, pixel_src, pat_argb, foreground,
                                         alpha_modes_, kD2dAlphaModes, color_multiply_modes_, global_src_,
                                         global_dst_);
    WriteDst(x, y, out_argb);
}

void VivanteDraw2d::ExecuteMonoStream(const uint8_t* stream, uint32_t stream_words, uint32_t rect_x0,
                                      uint32_t rect_y0, uint32_t rect_w, uint32_t rect_h, uint32_t x0, uint32_t y0,
                                      uint32_t w, uint32_t h) {
    const bool use_pattern = PatternUsed(rop_, use_pat_override_, true, true);
    const uint32_t base_src_w = src_size_x_;
    const uint32_t base_src_h = src_size_y_;
    uint32_t stream_width = rect_w;
    uint32_t stream_height = rect_h;
    ResolveMonoStreamExtent(src_x0_, src_relative_, stream_width, stream_width);
    ResolveMonoStreamExtent(src_y0_, src_relative_, stream_height, stream_height);
    for (uint32_t yi = 0; yi < h; ++yi) {
        const uint32_t y = (dst_cmd_ == 3u) ? (h - 1u - yi) : yi;
        const uint32_t dy = (y0 + y) - rect_y0;
        const uint32_t source_y_offset = StretchSourceOffset(y0 + y, rect_y0, stretch_y_);
        if (source_y_offset == UINT32_MAX)
            mem_.HaltUnsupported("imx6-vivante mono stretch sample outside guest-programmed extent", kD2dStretchY,
                                 (static_cast<uint64_t>(base_src_h) << 32) | stretch_y_);
        for (uint32_t xi = 0; xi < w; ++xi) {
            const uint32_t x = (dst_cmd_ == 3u) ? (w - 1u - xi) : xi;
            const uint32_t dx = (x0 + x) - rect_x0;
            const uint32_t stream_x = MonoStreamCoordinate(src_x0_, src_relative_, dx);
            const uint32_t stream_y = MonoStreamCoordinate(src_y0_, src_relative_, dy);
            uint32_t mono_bit = 0u;
            if (!VivanteBlitOps::ReadStreamMonoBit(stream, stream_x, stream_y, stream_width, stream_height,
                                                  src_pack_, stream_words, mono_bit))
                mem_.HaltUnsupported("imx6-vivante mono stream is shorter than its rectangle", kD2dSrcConfig,
                                     stream_words);
            const bool one = mono_bit != 0u;
            const bool mono_masked = pe_src_transparency_ == 1u;
            const MonoRopSelection mono_rop = SelectMonoRop(one, mono_masked, mono_transparent_one_);
            uint32_t dst_argb = 0u;
            uint32_t dst_packed = 0u;
            ReadDstActual(x0 + x, y0 + y, dst_argb, dst_packed);
            const bool destination_opaque = DestinationAccepts(dst_packed);
            if (use_pattern) PatternOpaque();
            const bool transparent = VivanteBlitOps::IsTransparentPixel(destination_opaque, false);
            const bool type3_rop = IsType3Rop();
            if (SkipTransparentPixel(transparent, type3_rop, alpha_enable_)) continue;
            const bool foreground = mono_rop.foreground && !transparent;
            const bool pixel_use_source =
                alpha_enable_ ? true
                              : ResolveRopResource(RopBranchUsesSource(rop_, foreground), use_src_override_);
            const bool pixel_use_pattern =
                alpha_enable_ ? false
                              : PatternUsed(rop_, use_pat_override_, false, foreground);
            const bool pixel_use_destination =
                (alpha_enable_ || use_destination_)
                    ? true
                    : ResolveRopResource(RopBranchUsesDestination(rop_, foreground), use_dst_override_);

            uint32_t src_argb = one ? src_fg_ : src_bg_;
            if (mono_masked && pixel_use_source && src_surface_configured_) {
                const uint32_t source_x_offset = StretchSourceOffset(x0 + x, rect_x0, stretch_x_);
                if (source_x_offset == UINT32_MAX)
                    mem_.HaltUnsupported("imx6-vivante mono stretch sample outside guest-programmed extent",
                                         kD2dStretchX, (static_cast<uint64_t>(base_src_w) << 32) | stretch_x_);
                uint32_t sx = 0u;
                uint32_t sy = 0u;
                if (!ResolveSourceCoordinate(src_x0_, src_relative_, x0 + x, source_x_offset, sx) ||
                    !ResolveSourceCoordinate(src_y0_, src_relative_, y0 + y, source_y_offset, sy))
                    continue;
                uint32_t src_packed = 0u;
                ReadSource(sx, sy, src_argb, src_packed);
            }
            src_argb = ResolveRopInput(rop_, foreground, true, pixel_use_source, src_argb, kD2dSrcConfig, src_cfg_);
            dst_argb = ResolveRopInput(rop_, foreground, false, pixel_use_destination, dst_argb,
                                       kD2dPeTransparency, mem_.StateReg(kD2dPeTransparency));
            const uint32_t pat_argb = pixel_use_pattern ? PatternPixel(x0 + x, y0 + y) : 0u;
            const uint32_t out_argb = BlendOrRop(alpha_enable_, rop_, dst_argb, src_argb, pat_argb,
                                                 foreground, alpha_modes_, kD2dAlphaModes, color_multiply_modes_,
                                                 global_src_, global_dst_);
            WriteDst(x0 + x, y0 + y, out_argb);
        }
    }
}

void VivanteDraw2d::ExecuteCopyLike(uint32_t rect_x0, uint32_t rect_y0, uint32_t x0, uint32_t y0,
                                    uint32_t w, uint32_t h) {
    const bool use_pattern = PatternUsed(rop_, use_pat_override_, true, true);
    auto sourceCoordinates = [&](uint32_t x, uint32_t y, uint32_t& sx, uint32_t& sy) {
        const uint32_t source_x_offset =
            StretchSourceOffset(x0 + x, rect_x0, stretch_x_);
        const uint32_t source_y_offset =
            StretchSourceOffset(y0 + y, rect_y0, stretch_y_);
        if (!ResolveSourceCoordinate(src_x0_, src_relative_, x0 + x, source_x_offset, sx) ||
            !ResolveSourceCoordinate(src_y0_, src_relative_, y0 + y, source_y_offset, sy))
            mem_.HaltUnsupported(kNegativeRelativeSourceOrigin, kD2dSrcOrigin, src_origin_);
    };

    source_snapshot_.clear();
    bool use_source_snapshot = false;

    if (dst_cmd_ != 4u) {
        uint64_t src_begin = UINT64_MAX;
        uint64_t src_end = 0u;
        uint64_t dst_begin = UINT64_MAX;
        uint64_t dst_end = 0u;
        bool src_range_valid = false;
        bool dst_range_valid = false;

        auto includeGpuRange = [&](uint32_t base, uint64_t offset, uint32_t bytes,
                                   uint64_t& range_begin, uint64_t& range_end, bool& range_valid) {
            uint32_t address = 0u;
            if (!AddGpuOffset(base, offset, address)) return;
            const uint64_t begin = address;
            const uint64_t end = begin + bytes;
            if (!range_valid || begin < range_begin) range_begin = begin;
            if (!range_valid || end > range_end) range_end = end;
            range_valid = true;
        };

        for (uint32_t py = 0u; py < h; ++py) {
            for (uint32_t px = 0u; px < w; ++px) {
                uint32_t sx = 0u, sy = 0u;
                sourceCoordinates(px, py, sx, sy);
                const DeCoord sp = SrcCoord(sx, sy);
                const SurfaceLocation sl = LocateSurface(src_stride_, sp.x, sp.y, src_bpp_, src_layout_);
                if (!sl.valid) continue;
                const uint32_t source_base = sl.plane == 0u ? src_addr_ : src_ex_addr_;
                includeGpuRange(source_base, sl.offset, src_bpp_, src_begin, src_end, src_range_valid);

                const DeCoord dp = DstCoord(x0 + px, y0 + py);
                const SurfaceLocation dl = LocateSurface(dst_stride_, dp.x, dp.y, dst_bpp_, dst_layout_);
                if (!dl.valid) continue;
                includeGpuRange(dst_addr_, dl.offset, dst_bpp_, dst_begin, dst_end, dst_range_valid);
            }
        }

        const bool base_host_alias = src_ptr_ == dst_ptr_ || (src_extra_ptr_ != nullptr && src_extra_ptr_ == dst_ptr_);
        use_source_snapshot = src_range_valid && dst_range_valid &&
                              (AddressRangesOverlap(src_begin, src_end, dst_begin, dst_end) || base_host_alias);
        if (use_source_snapshot) {
            const uint64_t snapshot_count = static_cast<uint64_t>(w) * h;
            if (snapshot_count > source_snapshot_.max_size())
                mem_.HaltUnsupported("imx6-vivante DRAW_2D source snapshot size overflow", src_addr_, snapshot_count);
            source_snapshot_.resize(static_cast<size_t>(snapshot_count));
            for (uint32_t py = 0u; py < h; ++py) {
                for (uint32_t px = 0u; px < w; ++px) {
                    uint32_t sx = 0u, sy = 0u;
                    sourceCoordinates(px, py, sx, sy);
                    const uint64_t snapshot_index = static_cast<uint64_t>(py) * w + px;
                    SourceSample& sample = source_snapshot_[static_cast<size_t>(snapshot_index)];
                    ReadSource(sx, sy, sample.argb, sample.packed);
                }
            }
        }
    }

    for (uint32_t yi = 0; yi < h; ++yi) {
        const uint32_t y = (dst_cmd_ == 3u) ? (h - 1u - yi) : yi;
        for (uint32_t xi = 0; xi < w; ++xi) {
            const uint32_t x = (dst_cmd_ == 3u) ? (w - 1u - xi) : xi;
            uint32_t src_argb = 0u;
            uint32_t src_packed = 0u;
            if (use_source_snapshot) {
                const uint64_t snapshot_index = static_cast<uint64_t>(y) * w + x;
                const SourceSample& sample = source_snapshot_[static_cast<size_t>(snapshot_index)];
                src_argb = sample.argb;
                src_packed = sample.packed;
            } else {
                uint32_t sx = 0u, sy = 0u;
                sourceCoordinates(x, y, sx, sy);
                ReadSource(sx, sy, src_argb, src_packed);
            }
            ExecutePixel(x0 + x, y0 + y, src_argb, &src_packed, use_pattern, true);
        }
    }
}

void VivanteDraw2d::ExecutePatternFill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h) {
    const bool use_pattern = PatternUsed(rop_, use_pat_override_, true, true);
    if (use_pattern && FastSolidPatternFill(x0, y0, w, h)) return;
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x) {
            ExecutePixel(x0 + x, y0 + y, 0u, nullptr, use_pattern, false);
        }
    }
}

}

}
