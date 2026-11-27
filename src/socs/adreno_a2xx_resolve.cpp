#include "adreno_a2xx_resolve.h"
#include "adreno_a2xx_dither.h"
#include "adreno_a2xx_socs.h"
#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <algorithm>

namespace cerfcore {

REGISTER_SERVICE(AdrenoA2xxResolve);

bool AdrenoA2xxResolve::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxResolve::OnReady() { memory_ = &emu_.Get<AdrenoA2xxMemory>(); }

void AdrenoA2xxResolve::Prepare(uint32_t format, uint32_t swap, uint32_t target_swap_field, bool truncate_8888,
                                uint32_t bytes, const AdrenoA2xxColorLayout& source_layout,
                                const AdrenoA2xxColorLayout& target_layout) {
    const uint32_t key = format | swap << 4 | target_swap_field << 6 | uint32_t(truncate_8888) << 8;
    if (table_.key == key) return;
    table_.key = key;
    table_.channels = bytes == 4u ? 4u : target_layout.PackedChannels();
    const bool flip = (swap == 1u) != (target_swap_field == 1u);
    for (unsigned i = 0; i < table_.channels; ++i) {
        const unsigned s = flip && (i == 0u || i == 2u) ? i ^ 2u : i;
        const uint32_t maximum = bytes == 4u ? 255u : source_layout.Maximum(s);
        table_.source[i] = static_cast<uint8_t>(s);
        table_.source_shift[i] = static_cast<uint8_t>(bytes == 4u ? 0u : source_layout.Shift(s));
        table_.source_maximum[i] = static_cast<uint8_t>(maximum);
        table_.target_shift[i] = static_cast<uint8_t>(bytes == 4u ? 0u : target_layout.Shift(i));
        table_.target_maximum[i] = static_cast<uint8_t>(bytes == 4u ? 255u : target_layout.Maximum(i));
        for (uint32_t v = 0; v <= maximum; ++v) {
            std::array<uint8_t,4> sample{};
            if (bytes == 4u) sample[s] = static_cast<uint8_t>(v);
            else cerf::le::Put16(sample.data(), static_cast<uint16_t>(v << source_layout.Shift(s)));
            const float channel = std::clamp(source_layout.Unpack(sample.data())[s],0.0f,1.0f);
            table_.value[i][v] = bytes == 4u
                ? static_cast<uint8_t>(truncate_8888 ? channel * 255u
                                                     : AdrenoA2xxApproximateDitherQuantize(channel,255u,i,0u,0u,false))
                : static_cast<uint8_t>(AdrenoA2xxApproximateDitherQuantize(channel,target_layout.Maximum(i),i,0u,0u,false));
        }
    }
}

void AdrenoA2xxResolve::Pixel(const Rows& rows, const uint8_t* from, uint8_t* destination) const {
    std::array<uint8_t,4> pixel{};
    AdrenoA2xxCopyPixelBytes(pixel.data(),destination,rows.bytes);
    if (rows.bytes == 4u) {
        for (unsigned i = 0; i < 4u; ++i)
            if (rows.target_mask & (1u << i)) pixel[i] = table_.value[i][from[table_.source[i]]];
    } else {
        const uint32_t packed_source = cerf::le::U16(from);
        uint32_t packed = cerf::le::U16(pixel.data());
        for (unsigned i = 0; i < table_.channels; ++i) if (rows.target_mask & (1u << i)) {
            const uint32_t v = (packed_source >> table_.source_shift[i]) & table_.source_maximum[i];
            packed = (packed & ~(uint32_t(table_.target_maximum[i]) << table_.target_shift[i])) |
                (uint32_t(table_.value[i][v]) << table_.target_shift[i]);
        }
        cerf::le::Put16(pixel.data(), static_cast<uint16_t>(packed));
    }
    AdrenoA2xxCopyPixelBytes(destination,pixel.data(),rows.bytes);
}

void AdrenoA2xxResolve::Clear(const Rows& rows, AdrenoA2xxPixelWrites& out, uint8_t* cleared) const {
    const AdrenoA2xxPixelWrite clear{cleared,rows.copy->ClearedDepth(cleared),rows.copy->DepthBytes()};
    if (rows.defer_clears) out.Push(clear);
    else {
        AdrenoA2xxCopyPixelBytes(cleared,clear.data.data(),clear.bytes);
        ++out.written;
    }
}

void AdrenoA2xxResolve::Row(const Rows& rows, AdrenoA2xxPixelWrites& out, AdrenoA2xxPageCache& targets,
                            int y, int xl, int xr) {
    const uint64_t row = uint64_t(y+rows.offset_y)*rows.target_pitch+rows.offset_x;
    const uint32_t clear_bytes = rows.copy->DepthBytes();
    const uint32_t bytes = rows.bytes;
    for (int x = xl; x < xr;) {
        const int run = (std::min)(xr - x, int(rows.source_layout->RunPixels(uint32_t(x))));
        const uint8_t* from = rows.source+rows.source_layout->Offset(uint32_t(x),uint32_t(y));
        uint8_t* cleared = rows.clear ? rows.clear+rows.clear_layout->Offset(uint32_t(x),uint32_t(y)) : nullptr;
        const uint64_t address = (row+uint64_t(x))*bytes;
        const uint32_t run_bytes = uint32_t(run)*bytes;
        uint8_t* destination = rows.target ? rows.target+address :
            memory_->CachedWrite(targets,uint64_t(rows.target_base)+address,run_bytes,*rows.mmu);
        const bool clear_disjoint = cleared == nullptr || rows.defer_clears ||
            cleared + size_t(run)*clear_bytes <= from || from + run_bytes <= cleared;
        if (destination && clear_disjoint) {
            if (cleared)
                for (int i = 0; i < run; ++i, cleared += clear_bytes) Clear(rows,out,cleared);
            for (int i = 0; i < run; ++i, from += bytes, destination += bytes) Pixel(rows,from,destination);
            out.written += uint64_t(run);
            x += run;
            continue;
        }
        for (const int end = x + run; x < end; ++x, from += bytes) {
            std::array<uint8_t,4> source{};
            AdrenoA2xxCopyPixelBytes(source.data(),from,bytes);
            if (cleared) {
                Clear(rows,out,cleared);
                cleared += clear_bytes;
            }
            const uint64_t pixel_address = (row+uint64_t(x))*bytes;
            AdrenoA2xxSpan span;
            uint8_t* const pixel_destination = rows.target ? rows.target+pixel_address :
                memory_->CachedOrSpanWrite(targets,uint64_t(rows.target_base)+pixel_address,bytes,*rows.mmu,span);
            Pixel(rows,source.data(),pixel_destination);
            ++out.written;
        }
    }
}

}
