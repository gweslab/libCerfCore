#include "small_page_nand_image.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/platform/host_error.h>

#include <cstring>
#include <utility>

namespace cerfcore {

SmallPageNandImage::SmallPageNandImage(CerfEmulator& emu, std::string name, uint32_t blocks)
    : emu_(emu), name_(std::move(name)), blocks_(blocks) {}

void SmallPageNandImage::Open(const std::string& path) {
    path_ = path;
    if (!file_.Open(path, host_platform::FileMode::kReadWriteAlways))
        emu_.Get<Fatal>().UserError("%s: cannot open '%s' (error %u)", name_.c_str(), path.c_str(),
                                    host_platform::LastErrorCode());
    uint64_t size = 0;
    if (!file_.Size(size))
        emu_.Get<Fatal>().UserError("%s: cannot read the size of '%s'", name_.c_str(), path.c_str());
    const uint64_t expected = static_cast<uint64_t>(blocks_) * kBlockBytes;
    if (size == 0) {
        for (uint32_t block = 0; block < blocks_; ++block)
            Fill(static_cast<uint64_t>(block) * kBlockBytes, kBlockBytes);
        LOG(Boot, "%s: created erased image '%s' (%llu bytes)\n", name_.c_str(), path.c_str(),
            static_cast<unsigned long long>(expected));
        return;
    }
    if (size != expected)
        emu_.Get<Fatal>().UserError("%s: '%s' holds %llu bytes; the part holds %llu", name_.c_str(),
                                    path.c_str(), static_cast<unsigned long long>(size),
                                    static_cast<unsigned long long>(expected));
    LOG(Boot, "%s: using image '%s'\n", name_.c_str(), path.c_str());
}

void SmallPageNandImage::Seek(uint64_t offset) {
    if (!file_.SeekTo(offset))
        emu_.Get<Fatal>().UserError("%s: seek to %llu in '%s' failed", name_.c_str(),
                                    static_cast<unsigned long long>(offset), path_.c_str());
}

void SmallPageNandImage::Fill(uint64_t offset, uint32_t length) {
    uint8_t erased[kBlockBytes];
    std::memset(erased, kErased, length);
    Seek(offset);
    uint32_t done = 0;
    if (!file_.Write(erased, length, done) || done != length)
        emu_.Get<Fatal>().UserError("%s: write to '%s' failed", name_.c_str(), path_.c_str());
}

void SmallPageNandImage::ReadPage(uint32_t page, uint8_t* dst) {
    Seek(static_cast<uint64_t>(page) * kPageBytes);
    uint32_t done = 0;
    if (!file_.Read(dst, kPageBytes, done) || done != kPageBytes)
        emu_.Get<Fatal>().UserError("%s: read of page %u from '%s' failed", name_.c_str(), page,
                                    path_.c_str());
}

void SmallPageNandImage::WritePage(uint32_t page, const uint8_t* src) {
    Seek(static_cast<uint64_t>(page) * kPageBytes);
    uint32_t done = 0;
    if (!file_.Write(src, kPageBytes, done) || done != kPageBytes)
        emu_.Get<Fatal>().UserError("%s: write of page %u to '%s' failed", name_.c_str(), page,
                                    path_.c_str());
}

void SmallPageNandImage::EraseBlock(uint32_t block) {
    Fill(static_cast<uint64_t>(block) * kBlockBytes, kBlockBytes);
}

}
