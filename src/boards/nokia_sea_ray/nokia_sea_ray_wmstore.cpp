#include "nokia_sea_ray_wmstore.h"

#include "../../peripherals/mmc/emmc_card_base.h"
#include "../../peripherals/mmc/mmc_card.h"
#include "../../storage/fat_volume_layout.h"
#include "nokia_sea_ray_emmc_layout.h"
#include "nokia_sea_ray_family_id.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <set>

namespace cerfcore {

REGISTER_SERVICE(NokiaSeaRayWmstore);

namespace {

namespace ri  = cerf::rom_image_parse;
namespace fvl = fat_volume_layout;

constexpr unsigned kMbrPartitions = 4u;
constexpr unsigned kEbrStoreEntry = 0u;
constexpr unsigned kEbrLinkEntry  = 1u;

}

bool NokiaSeaRayWmstore::ShouldRegister() {
    return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NokiaSeaRay;
}

EmmcCardBase& NokiaSeaRayWmstore::Card() {
    return static_cast<EmmcCardBase&>(emu_.Get<MmcCard>());
}

std::vector<uint8_t> NokiaSeaRayWmstore::ReadSectors(uint64_t first, uint64_t count) {
    return Card().ReadStoredSectors(first, count);
}

bool NokiaSeaRayWmstore::StoreAt(uint64_t lba, Store& out) {
    uint32_t superblock = 0;
    if (!ri::WmstoreStoreHeader(ReadSectors(lba, 1u), Card().StoredSectorCount(), superblock))
        return false;
    out = {lba, superblock};
    return true;
}

bool NokiaSeaRayWmstore::StoreInExtendedChain(uint64_t base, Store& out) {
    std::set<uint64_t> visited;
    for (uint64_t next = 0;;) {
        const uint64_t at = base + next;
        if (!visited.insert(at).second) {
            emu_.Get<Fatal>().Die("eMMC store: the extended partition chain at sector %llu "
                                  "loops back to sector %llu",
                                  static_cast<unsigned long long>(base),
                                  static_cast<unsigned long long>(at));
        }
        const std::vector<uint8_t> ebr = ReadSectors(at, 1u);
        if (!fvl::HasBootSignature(ebr.data())) return false;
        const fvl::MbrPartition entry = fvl::ReadMbrPartition(ebr.data(), kEbrStoreEntry);
        if (entry.type == kNokiaSeaRayPartitionStore && StoreAt(at + entry.start_lba, out))
            return true;
        const fvl::MbrPartition link = fvl::ReadMbrPartition(ebr.data(), kEbrLinkEntry);
        if (link.start_lba == 0u || link.sectors == 0u) return false;
        next = link.start_lba;
    }
}

NokiaSeaRayWmstore::Store NokiaSeaRayWmstore::FindStore() {
    const std::vector<uint8_t> mbr = ReadSectors(0u, 1u);
    Store    store;
    uint32_t superblock = 0;
    if (ri::WmstoreStoreHeader(mbr, Card().StoredSectorCount(), superblock)) return {0u, superblock};
    for (unsigned i = 0; i < kMbrPartitions; ++i) {
        const fvl::MbrPartition p = fvl::ReadMbrPartition(mbr.data(), i);
        if (p.type == kNokiaSeaRayPartitionStore && StoreAt(p.start_lba, store)) return store;
        if (p.type == kNokiaSeaRayPartitionExtended && StoreInExtendedChain(p.start_lba, store))
            return store;
    }
    emu_.Get<Fatal>().Die("eMMC store: the card holds no _wmstore store");
}

bool NokiaSeaRayWmstore::FindPartition(const char* name, ri::WmstorePartition& out) {
    const Store    store      = FindStore();
    const uint64_t superblock = store.base + store.superblock;
    uint32_t       entries    = 0;
    if (!ri::WmstoreSuperblock(ReadSectors(superblock, 1u), entries)) {
        emu_.Get<Fatal>().Die("eMMC store: sector %llu holds no _wmstore superblock",
                              static_cast<unsigned long long>(superblock));
    }
    for (uint32_t i = 0; i < entries; ++i) {
        ri::WmstorePartition entry;
        if (!ri::WmstorePartitionEntry(ReadSectors(superblock + 1u + i, 1u), entry)) break;
        if (entry.name != name) continue;
        entry.start_lba += store.base;
        out = std::move(entry);
        return true;
    }
    return false;
}

}
