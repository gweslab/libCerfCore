#include "adreno_a2xx_pixel_writes.h"

#include <algorithm>
#include <utility>

namespace cerfcore {

namespace {

constexpr size_t kFirstPixelCapacity = 256u;

}

void AdrenoA2xxPixelWrites::Grow() {
    const size_t grown = capacity ? capacity * 2u : kFirstPixelCapacity;
    std::unique_ptr<AdrenoA2xxPixelWrite[]> larger(new AdrenoA2xxPixelWrite[grown]);
    std::copy_n(pixels.get(), size, larger.get());
    pixels   = std::move(larger);
    capacity = grown;
}

}
