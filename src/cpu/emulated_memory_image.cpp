#include "emulated_memory_image.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/state/state_image_format.h>
#include <cerfcore/state/state_stream.h>
#include <cstring>

namespace cerfcore {

REGISTER_SERVICE(EmulatedMemoryImage);

void EmulatedMemoryImage::OnReady() { memory_ = &emu_.Get<EmulatedMemory>(); }

void EmulatedMemoryImage::WipeVolatileRegions() {
    const size_t n = memory_->count_.load(std::memory_order_acquire);
    for (size_t i = 0; i < n; ++i) {
        EmulatedMemory::Region& r = memory_->regions_[i];
        if (EmulatedMemory::IsFlash(r)) {
            continue;
        }
        if (uint8_t* host = r.host_ptr.load(std::memory_order_acquire)) {
            std::memset(host, 0, r.size);
            LOG(Mem, "WipeVolatileRegions: region 0x%08X size 0x%X zeroed\n",
                r.base, r.size);
        }
    }
}

void EmulatedMemoryImage::SaveState(StateWriter& w) { SaveRegions(w, false); }

void EmulatedMemoryImage::SaveFlashRegions(StateWriter& w) { SaveRegions(w, true); }

void EmulatedMemoryImage::RestoreState(StateReader& r) { RestoreRegions(r, false, true); }

void EmulatedMemoryImage::RestoreFlashRegions(StateReader& r) { RestoreRegions(r, true, true); }

uint32_t EmulatedMemoryImage::RegionCount(bool flash) const {
    const size_t n = memory_->count_.load(std::memory_order_acquire);
    uint32_t count = 0;
    for (size_t i = 0; i < n; ++i) {
        if (EmulatedMemory::InStateImage(memory_->regions_[i], flash)) ++count;
    }
    return count;
}

void EmulatedMemoryImage::SaveRegions(StateWriter& w, bool flash) {
    const size_t n = memory_->count_.load(std::memory_order_acquire);
    w.Write(flash ? "flash_count" : "volatile_count", RegionCount(flash));
    for (size_t i = 0; i < n; ++i) {
        EmulatedMemory::Region& r = memory_->regions_[i];
        if (!EmulatedMemory::InStateImage(r, flash)) continue;
        w.Write("base", r.base);
        w.Write("region_size", r.size);
        uint8_t* host = r.host_ptr.load(std::memory_order_acquire);
        const uint8_t backed = host ? 1u : 0u;
        w.Write("backed", backed);
        if (host) w.WriteBytes("host", host, r.size);
    }
}

void EmulatedMemoryImage::RestoreRegions(StateReader& r, bool flash, bool write) {
    const char* kind = flash ? "flash" : "RAM";
    uint32_t count = 0;
    r.Read(flash ? "flash_count" : "volatile_count", count);
    if (count != RegionCount(flash))
        r.Reject("the image has %u %s regions, this build has %u", count, kind,
                 RegionCount(flash));
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t base = 0, size = 0;
        r.Read("base", base);
        r.Read("region_size", size);
        EmulatedMemory::Region* reg = memory_->FindBackingRegion(base);
        if (!reg || reg->base != base || reg->size != size || !EmulatedMemory::InStateImage(*reg, flash))
            r.Reject("%s region base=0x%08X size=0x%X is not in this memory map",
                     kind, base, size);
        uint8_t backed = 0;
        r.Read("backed", backed);
        if (!write) {
            if (backed) r.SkipBytes("host", size);
        } else if (backed) {
            r.ReadBytes("host", memory_->EnsureBacked(reg), size);
        } else if (uint8_t* host = reg->host_ptr.load(std::memory_order_acquire)) {
            std::memset(host, 0, size);
        }
    }
}

void EmulatedMemoryImage::RestoreSections(StateReader& r, bool write, bool with_flash) {
    while (r.Remaining() != 0) {
        const auto section = static_cast<StateSection>(r.EnterFrame());
        if (section != StateSection::Ram && (section != StateSection::Flash || !with_flash)) {
            r.SkipFrame();
            continue;
        }
        RestoreRegions(r, section == StateSection::Flash, write);
        r.LeaveFrame();
    }
}

}
