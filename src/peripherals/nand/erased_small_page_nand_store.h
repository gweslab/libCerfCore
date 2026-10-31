#pragma once

#include "small_page_nand_flash.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class CerfEmulator;

class ErasedSmallPageNandStore final : public SmallPageNandStore {
public:
    ErasedSmallPageNandStore(CerfEmulator& emu, std::string name);

    void ReadPage  (uint32_t page,  uint8_t* dst)       override;
    void WritePage (uint32_t page,  const uint8_t* src) override;
    void EraseBlock(uint32_t block)                     override;

private:
    CerfEmulator& emu_;
    std::string   name_;
};

}
