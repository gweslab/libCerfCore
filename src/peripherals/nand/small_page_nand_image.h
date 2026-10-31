#pragma once

#include "small_page_nand_flash.h"

#include <cerfcore/platform/host_file.h>

#include <cstdint>
#include <string>

namespace cerfcore {

class CerfEmulator;

class SmallPageNandImage final : public SmallPageNandStore {
public:
    SmallPageNandImage(CerfEmulator& emu, std::string name, uint32_t blocks);

    void Open(const std::string& path);

    void ReadPage  (uint32_t page,  uint8_t* dst)       override;
    void WritePage (uint32_t page,  const uint8_t* src) override;
    void EraseBlock(uint32_t block)                     override;

private:
    static constexpr uint32_t kBlockBytes = kPageBytes * kPagesPerBlock;

    void Seek(uint64_t offset);
    void Fill(uint64_t offset, uint32_t length);

    CerfEmulator&           emu_;
    std::string             name_;
    std::string             path_;
    uint32_t                blocks_;
    host_platform::HostFile file_;
};

}
