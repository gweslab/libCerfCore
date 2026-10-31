#include "erased_small_page_nand_store.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>

#include <cstring>
#include <utility>

namespace cerfcore {

ErasedSmallPageNandStore::ErasedSmallPageNandStore(CerfEmulator& emu, std::string name)
    : emu_(emu), name_(std::move(name)) {}

void ErasedSmallPageNandStore::ReadPage(uint32_t, uint8_t* dst) {
    std::memset(dst, kErased, kPageBytes);
}

void ErasedSmallPageNandStore::WritePage(uint32_t page, const uint8_t*) {
    emu_.Get<Fatal>().Die("%s: page program of page %u; the part holds no storage", name_.c_str(), page);
}

void ErasedSmallPageNandStore::EraseBlock(uint32_t block) {
    emu_.Get<Fatal>().Die("%s: erase of block %u; the part holds no storage", name_.c_str(), block);
}

}
