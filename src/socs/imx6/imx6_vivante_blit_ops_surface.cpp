#include "imx6_vivante_blit_ops.h"

namespace cerfcore {

namespace imx6_vivante {

bool VivanteBlitCoordinateOps::ResolveSourceCoordinate(uint32_t origin_field, bool relative, uint32_t destination_coordinate,
                                                       uint32_t rectangle_offset, uint32_t& source_coordinate) {
    const int64_t coordinate =
        relative ? static_cast<int64_t>(destination_coordinate) + static_cast<int16_t>(origin_field & 0xFFFFu)
                 : static_cast<int64_t>(origin_field & 0xFFFFu) + rectangle_offset;
    if (coordinate < 0) return false;
    source_coordinate = static_cast<uint32_t>(coordinate);
    return true;
}

uint32_t VivanteBlitCoordinateOps::MonoStreamCoordinate(uint32_t origin_field, bool relative, uint32_t rectangle_offset) {
    return relative ? rectangle_offset : (origin_field & 0xFFFFu) + rectangle_offset;
}

void VivanteBlitCoordinateOps::ResolveMonoStreamExtent(uint32_t origin_field, bool relative, uint32_t rectangle_extent,
                                                       uint32_t& stream_extent) {
    const uint64_t extent = relative ? static_cast<uint64_t>(rectangle_extent)
                                     : static_cast<uint64_t>(origin_field & 0xFFFFu) + rectangle_extent;
    stream_extent = static_cast<uint32_t>(extent);
}

uint32_t VivanteBlitCoordinateOps::StretchCoordinate(uint32_t destination_offset, uint32_t programmed_factor,
                                                     bool gdi_stretch) {
    const uint64_t factor = programmed_factor;
    const uint64_t product = static_cast<uint64_t>(destination_offset) * factor;
    const uint64_t coordinate = (product >> 16) + (!gdi_stretch && (product & 0x8000u) != 0u ? 1u : 0u);
    return static_cast<uint32_t>(coordinate);
}

uint32_t VivanteBlitSurfaceOps::SurfaceWidthFromStride(uint32_t stride, uint32_t bpp, SurfaceLayout layout) {
    if (layout == SurfaceLayout::Linear) return stride / bpp;
    return stride / (4u * bpp);
}

VivanteBlitSurfaceOps::SurfaceLayout VivanteBlitSurfaceOps::DecodeSurfaceLayout(bool tiled, bool multi_tiled,
                                                                                bool supertiled) {
    if (multi_tiled) return supertiled ? SurfaceLayout::MultiSuperTiled : SurfaceLayout::MultiTiled;
    if (supertiled) return SurfaceLayout::SuperTiled;
    return tiled ? SurfaceLayout::Tiled : SurfaceLayout::Linear;
}

VivanteBlitSurfaceOps::SurfaceLocation VivanteBlitSurfaceOps::LocateSurface(uint32_t stride, uint32_t x, uint32_t y, uint32_t bpp,
                                                                            SurfaceLayout layout) {
    const int64_t sx = x;
    const int64_t sy = y;
    const int64_t pixel_bytes = bpp;
    const int64_t row_bytes = stride;
    int64_t offset = 0;
    uint32_t plane = 0u;
    if (layout == SurfaceLayout::Linear) {
        offset = sy * row_bytes + sx * pixel_bytes;
    } else {
        const int64_t tile_bytes = 16 * pixel_bytes;
        const int64_t tx = sx >> 2;
        const int64_t ty = sy >> 2;
        const int64_t in_tile = (((sy & 3) << 2) | (sx & 3)) * pixel_bytes;
        if (layout == SurfaceLayout::Tiled) {
            offset = ty * row_bytes + tx * tile_bytes + in_tile;
        } else if (layout == SurfaceLayout::MultiTiled) {
            plane = static_cast<uint32_t>((tx ^ ty) & 1);
            const int64_t local_tx = (tx & ~1ll) | (ty & 1);
            const int64_t local_ty = ty >> 1;
            offset = local_ty * row_bytes + local_tx * tile_bytes + in_tile;
        } else if (layout == SurfaceLayout::MultiSuperTiled) {
            const int64_t mx = tx >> 4;
            const int64_t my = ty >> 5;
            const int64_t lx = tx & 15;
            const int64_t ly = ty & 31;
            plane = static_cast<uint32_t>(((lx >> 1) ^ ly) & 1);
            const int64_t local_tile = ((ly & 24) << 3) + ((lx >> 2) << 4) + ((ly & 1) << 3) + (ly & 6) + (lx & 1);
            offset = my * 16 * row_bytes + mx * 256 * tile_bytes + local_tile * tile_bytes + in_tile;
        } else {
            const int64_t tile_x = tx >> 4;
            const int64_t tile_y = ty >> 4;
            const int64_t lx = tx & 15;
            const int64_t ly = ty & 15;
            const int64_t local_tile = ((ly >> 2) * 64) + ((lx >> 1) * 8) + ((ly & 3) * 2) + (lx & 1);
            offset = tile_y * 16 * row_bytes + tile_x * 256 * tile_bytes + local_tile * tile_bytes + in_tile;
        }
    }
    return {plane, static_cast<uint64_t>(offset), true};
}

VivanteBlitSurfaceOps::DeCoord VivanteBlitSurfaceOps::TransformDeCoord(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                                                       uint32_t rot, uint32_t mirror) {
    rot &= 7u;
    mirror &= 3u;

    if (width == 0u || height == 0u || x >= width || y >= height) return {0u, 0u, false};

    uint32_t tx = x;
    uint32_t ty = y;
    switch (rot) {
    case 1:  tx = width - 1u - x; break;
    case 2:  ty = height - 1u - y; break;
    case 4:
        tx = height - 1u - y;
        ty = x;
        break;
    case 5:
        tx = width - 1u - x;
        ty = height - 1u - y;
        break;
    case 6:
        tx = y;
        ty = width - 1u - x;
        break;
    default: break;
    }

    const uint32_t rw = (rot == 4u || rot == 6u) ? height : width;
    const uint32_t rh = (rot == 4u || rot == 6u) ? width : height;
    if (mirror & 1u) tx = rw - 1u - tx;
    if (mirror & 2u) ty = rh - 1u - ty;
    return {tx, ty};
}

}

}
