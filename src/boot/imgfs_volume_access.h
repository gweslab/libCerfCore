#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace cerfcore {

class ImgfsVolumeAccess {
public:
    virtual ~ImgfsVolumeAccess() = default;

    virtual uint32_t             SectorBytes() const                                    = 0;
    virtual std::vector<uint8_t> Read(uint32_t offset, uint32_t length)                 = 0;
    virtual void                 Patch(uint32_t offset, std::span<const uint8_t> bytes) = 0;
};

}
