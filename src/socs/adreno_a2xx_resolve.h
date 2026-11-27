#pragma once
#include "adreno_a2xx_color_layout.h"
#include "adreno_a2xx_gmem_layout.h"
#include "adreno_a2xx_memory.h"
#include "adreno_a2xx_mmu.h"
#include "adreno_a2xx_pixel_writes.h"
#include "adreno_a2xx_resolve_copy.h"
#include <cerfcore/core/service.h>
#include <array>
#include <cstdint>

namespace cerfcore {

class AdrenoA2xxResolve : public Service {
public:
    struct Rows {
        const uint8_t*               source        = nullptr;
        const AdrenoA2xxGmemLayout*  source_layout = nullptr;
        uint8_t*                     clear         = nullptr;
        const AdrenoA2xxGmemLayout*  clear_layout  = nullptr;
        bool                         defer_clears  = false;
        const AdrenoA2xxResolveCopy* copy          = nullptr;
        uint8_t*                     target        = nullptr;
        uint32_t                     target_base   = 0;
        uint32_t                     target_pitch  = 0;
        uint32_t                     offset_x      = 0;
        uint32_t                     offset_y      = 0;
        uint32_t                     bytes         = 0;
        uint32_t                     target_mask   = 0;
        const AdrenoA2xxMmu*         mmu           = nullptr;
    };

    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;

    void Prepare(uint32_t format, uint32_t swap, uint32_t target_swap_field, bool truncate_8888, uint32_t bytes,
                 const AdrenoA2xxColorLayout& source_layout, const AdrenoA2xxColorLayout& target_layout);
    void Row(const Rows& rows, AdrenoA2xxPixelWrites& out, AdrenoA2xxPageCache& targets, int y, int xl, int xr);

private:
    struct Table {
        uint32_t key = 0xFFFFFFFFu;
        uint32_t channels = 0;
        std::array<uint8_t, 4> source{};
        std::array<uint8_t, 4> source_shift{};
        std::array<uint8_t, 4> source_maximum{};
        std::array<uint8_t, 4> target_shift{};
        std::array<uint8_t, 4> target_maximum{};
        std::array<std::array<uint8_t, 256>, 4> value{};
    };

    void Pixel(const Rows& rows, const uint8_t* from, uint8_t* destination) const;
    void Clear(const Rows& rows, AdrenoA2xxPixelWrites& out, uint8_t* cleared) const;

    AdrenoA2xxMemory* memory_ = nullptr;
    Table             table_;
};

}
