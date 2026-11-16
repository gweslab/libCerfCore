#include "imx6_vivante_blit.h"

namespace cerfcore {

namespace imx6_vivante {

VivanteBlit::VivanteBlit(VivanteState& s, VivanteMem& mem) : draw2d_(s, mem), vr_(mem), rs_(mem) {}

void VivanteBlit::ExecuteDraw2d(const uint32_t* rect_words, uint32_t rect_count, const uint32_t* stream_data,
                                uint32_t stream_words) {
    draw2d_.Execute(rect_words, rect_count, stream_data, stream_words);
}

void VivanteBlit::ExecuteVideoRasterizer(uint32_t start_value) { vr_.Execute(start_value); }

void VivanteBlit::ExecuteRs() { rs_.ExecuteRs(); }

void VivanteBlit::ExecuteRsInPlace(uint32_t tile_count) { rs_.ExecuteRsInPlace(tile_count); }

void VivanteBlit::DecodeKick(uint32_t byte_off, uint32_t value, VivanteBlitKick& kick) {
    if (byte_off == 0x01294u && (value & (1u << 3)) == 0u) {
        kick.video_rasterizer = true;
        kick.video_rasterizer_start = value & 3u;
    }
    if (byte_off == 0x01600u && value != 0u) kick.rasterizer = true;
    if (byte_off == 0x016B0u && value != 0u) {
        kick.rasterizer_in_place = true;
        kick.rasterizer_tiles = value;
    }
}

void VivanteBlit::ExecuteKick(const VivanteBlitKick& kick) {
    if (kick.video_rasterizer) ExecuteVideoRasterizer(kick.video_rasterizer_start);
    if (kick.rasterizer) ExecuteRs();
    if (kick.rasterizer_in_place) ExecuteRsInPlace(kick.rasterizer_tiles);
}

}

}
