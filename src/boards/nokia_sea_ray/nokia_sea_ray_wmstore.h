#pragma once

#include "../../boot/rom_wmstore_parse.h"

#include <cerfcore/core/service.h>

#include <cstdint>
#include <vector>

namespace cerfcore {

class EmmcCardBase;

class NokiaSeaRayWmstore : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    bool FindPartition(const char* name, cerf::rom_image_parse::WmstorePartition& out);

private:
    struct Store {
        uint64_t base       = 0;
        uint32_t superblock = 0;
    };

    EmmcCardBase&        Card();
    std::vector<uint8_t> ReadSectors(uint64_t first, uint64_t count);
    bool                 StoreAt(uint64_t lba, Store& out);
    bool                 StoreInExtendedChain(uint64_t base, Store& out);
    Store                FindStore();
};

}
