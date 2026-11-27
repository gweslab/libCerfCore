#pragma once

#include "ce_imgfs_walker.h"
#include "imgfs_volume_access.h"

#include <cerfcore/core/service.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace cerfcore {

class ImgfsStubInjector : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    int ReplaceVictims(ImgfsVolumeAccess& volume);

private:
    struct Layout {
        uint32_t first_block = 0;
        uint32_t block_bytes = 0;
        uint32_t chunk_bytes = 0;
        uint32_t compression = 0;
    };

    Layout ReadLayout(ImgfsVolumeAccess& volume);
    std::set<uint32_t> FreeChunks(const cerf::ce_imgfs_walker::VolumeRead&        read,
                                  const std::vector<cerf::ce_imgfs_walker::LoadedDirent>& dirents,
                                  const cerf::ce_imgfs_walker::ImgfsModule&       victim,
                                  const Layout&                                   layout);
    void Replace(ImgfsVolumeAccess& volume, const cerf::ce_imgfs_walker::VolumeRead& read,
                 const cerf::ce_imgfs_walker::ImgfsModule& victim, std::set<uint32_t> pool,
                 const Layout& layout, const std::string& stub_path);
};

}
