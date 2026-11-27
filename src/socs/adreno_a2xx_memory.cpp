#include "adreno_a2xx_memory.h"
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/cpu/emulated_memory.h>
#include <cerfcore/boards/board_context.h>
#include "adreno_a2xx_socs.h"

#include <algorithm>
#include <utility>
#include <vector>

namespace cerfcore {

namespace {

constexpr uint32_t kMmuEnable     = 1u << 0;
constexpr uint32_t kBehaviorShift = 4u;
constexpr uint32_t kBehaviorMask  = 3u;
constexpr uint32_t kBehaviorNever = 0u;
constexpr uint32_t kVaBaseMask    = 0xFFFFF000u;
constexpr uint32_t kVaRegionsMask = 0x00000FFFu;
constexpr uint32_t kVaRegionShift = 16u;
constexpr uint32_t kPageShift     = 12u;
constexpr uint64_t kPageOffset    = (uint64_t(1) << kPageShift) - 1u;
constexpr uint32_t kPteFrameMask  = 0xFFFFF000u;
constexpr uint32_t kPteWrite      = 1u << 0;
constexpr uint32_t kPteRead       = 1u << 1;
constexpr uint32_t kPteBytes      = 4u;

uint64_t VaBase(const AdrenoA2xxMmu& mmu) { return mmu.va_range & kVaBaseMask; }

uint64_t VaEnd(const AdrenoA2xxMmu& mmu) {
    return VaBase(mmu) + (uint64_t(mmu.va_range & kVaRegionsMask) << kVaRegionShift);
}

bool TranslatesAny(const AdrenoA2xxMmu& mmu) {
    return (mmu.config & kMmuEnable) != 0u && ((mmu.config >> kBehaviorShift) & kBehaviorMask) != kBehaviorNever;
}

}

REGISTER_SERVICE(AdrenoA2xxMemory);

bool AdrenoA2xxMemory::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocHasAdrenoA2xx(board->GetSocId());
}

void AdrenoA2xxMemory::OnReady() {
    memory_ = &emu_.Get<EmulatedMemory>();
}

AdrenoA2xxSpan AdrenoA2xxMemory::ReadSpan(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config) {
    return MemorySpan(address, size, false, mmu_config);
}
AdrenoA2xxSpan AdrenoA2xxMemory::WriteSpan(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config) {
    return MemorySpan(address, size, true, mmu_config);
}

bool AdrenoA2xxMemory::Translates(uint64_t address, const AdrenoA2xxMmu& mmu) const {
    return TranslatesAny(mmu) && address >= VaBase(mmu) && address < VaEnd(mmu);
}

uint8_t* AdrenoA2xxMemory::Translate(uint64_t physical, uint64_t size, bool write, const AdrenoA2xxMmu& mmu) {
    return mmu.pixel_stage ? memory_->TryTranslateRangeUnfenced(physical, size, write)
                           : memory_->TryTranslateRange(physical, size, write);
}

uint8_t* AdrenoA2xxMemory::HostRange(uint64_t physical, uint64_t size, bool write, const AdrenoA2xxMmu& mmu) {
    uint8_t* p = Translate(physical, size, write, mmu);
    if (!p) emu_.Get<Fatal>().Die("GPU memory rejected %s at 0x%08X (size 0x%016llX)",
                                 write ? "write range" : "read range", static_cast<uint32_t>(physical),
                                 static_cast<unsigned long long>(size));
    return p;
}

const uint8_t* AdrenoA2xxMemory::UntranslatedRead(uint64_t address, uint64_t size,
                                                  const AdrenoA2xxMmu& mmu_config) {
    return Translates(address, mmu_config) ? nullptr : Untranslated(address, size, false, mmu_config);
}
const uint8_t* AdrenoA2xxMemory::CachedRead(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size,
                                            const AdrenoA2xxMmu& mmu) {
    return CachedPage(cache, address, size, false, mmu);
}

uint8_t* AdrenoA2xxMemory::CachedWrite(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size,
                                       const AdrenoA2xxMmu& mmu) {
    return CachedPage(cache, address, size, true, mmu);
}

uint8_t* AdrenoA2xxMemory::CachedOrSpanWrite(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size,
                                             const AdrenoA2xxMmu& mmu, AdrenoA2xxSpan& fallback) {
    if (uint8_t* cached = CachedWrite(cache, address, size, mmu)) return cached;
    fallback = WriteSpan(address, size, mmu);
    return fallback.data();
}

uint8_t* AdrenoA2xxMemory::CachedPage(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size, bool write,
                                      const AdrenoA2xxMmu& mmu) {
    const uint64_t page  = address >> kPageShift;
    auto&          entry = cache.entries[page % AdrenoA2xxPageCache::kEntries];
    if (entry.generation == cache.generation && entry.page == page) return entry.host + (address & kPageOffset);
    if (!Translates(address, mmu) || address + size > VaEnd(mmu) ||
        (address & kPageOffset) + size > kPageOffset + 1u) return nullptr;
    uint8_t* host = Translate(PagePhysical(page, write, mmu), kPageOffset + 1u, write, mmu);
    if (!host) return nullptr;
    entry = {page, host, cache.generation};
    return host + (address & kPageOffset);
}

uint8_t* AdrenoA2xxMemory::UntranslatedWrite(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config) {
    return Translates(address, mmu_config) ? nullptr : Untranslated(address, size, true, mmu_config);
}
void AdrenoA2xxMemory::RequireReadable(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config) {
    PageRuns(address, size, false, mmu_config, nullptr);
}
void AdrenoA2xxMemory::RequireWritable(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config) {
    PageRuns(address, size, true, mmu_config, nullptr);
}

AdrenoA2xxSpan AdrenoA2xxMemory::MemorySpan(uint64_t address, uint64_t size, bool write,
                                            const AdrenoA2xxMmu& mmu) {
    std::vector<AdrenoA2xxSpan::Segment> runs;
    if (uint8_t* host = PageRuns(address, size, write, mmu, &runs))
        return AdrenoA2xxSpan(host, static_cast<size_t>(size));
    return AdrenoA2xxSpan(std::move(runs), static_cast<size_t>(size), write);
}

uint8_t* AdrenoA2xxMemory::Untranslated(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu) {
    if ((mmu.config & kMmuEnable) != 0u && (address < mmu.mpu_base || address + size > mmu.mpu_end))
        emu_.Get<Fatal>().Die("GPU memory access at 0x%08X (size 0x%016llX) is outside the "
                              "MPU range 0x%08X..0x%08X", static_cast<uint32_t>(address),
                              static_cast<unsigned long long>(size), mmu.mpu_base, mmu.mpu_end);
    if (TranslatesAny(mmu) && address < VaBase(mmu) && address + size > VaBase(mmu))
        emu_.Get<Fatal>().Die("GPU memory access at 0x%08X (size 0x%016llX) runs into the "
                              "translated range at 0x%08X", static_cast<uint32_t>(address),
                              static_cast<unsigned long long>(size), static_cast<uint32_t>(VaBase(mmu)));
    return HostRange(address, size, write, mmu);
}

uint8_t* AdrenoA2xxMemory::PageRuns(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu,
                                    std::vector<AdrenoA2xxSpan::Segment>* runs) {
    if (!Translates(address, mmu)) return Untranslated(address, size, write, mmu);
    if (address + size > VaEnd(mmu))
        emu_.Get<Fatal>().Die("GPU memory span at 0x%08X (size 0x%016llX) runs past the "
                              "translated range end 0x%08X", static_cast<uint32_t>(address),
                              static_cast<unsigned long long>(size), static_cast<uint32_t>(VaEnd(mmu)));
    const uint64_t first = address >> kPageShift;
    const uint64_t last  = (size == 0u ? address : address + size - 1u) >> kPageShift;
    uint8_t* start = nullptr;
    uint8_t* next  = nullptr;
    bool contiguous = true;
    for (uint64_t page = first; page <= last; ++page) {
        const uint64_t begin = std::max(address, page << kPageShift);
        const uint64_t end   = std::min(address + size, (page + 1u) << kPageShift);
        const size_t bytes = static_cast<size_t>(end - begin);
        uint8_t* host = HostRange(PagePhysical(page, write, mmu) | (begin & kPageOffset), bytes, write, mmu);
        if (page == first) {
            start = host;
        } else if (host == next) {
            if (!contiguous && runs) runs->back().bytes += bytes;
        } else {
            if (contiguous && runs) runs->push_back({start, static_cast<size_t>(next - start)});
            contiguous = false;
            if (runs) runs->push_back({host, bytes});
        }
        next = host + bytes;
    }
    return contiguous ? start : nullptr;
}

uint64_t AdrenoA2xxMemory::PagePhysical(uint64_t page, bool write, const AdrenoA2xxMmu& mmu) {
    const uint32_t entry = ReadPageTableEntry(page - (VaBase(mmu) >> kPageShift), mmu);
    if ((entry & (write ? kPteWrite : kPteRead)) == 0u)
        emu_.Get<Fatal>().Die("GPU MMU %s fault at 0x%08X (page table entry 0x%08X)",
                              write ? "write" : "read", static_cast<uint32_t>(page << kPageShift), entry);
    return entry & kPteFrameMask;
}

uint32_t AdrenoA2xxMemory::ReadPageTableEntry(uint64_t index, const AdrenoA2xxMmu& mmu) {
    const uint64_t address = uint64_t(mmu.pt_base) + index * kPteBytes;
    const uint8_t* entry = Translate(address, kPteBytes, false, mmu);
    if (!entry)
        emu_.Get<Fatal>().Die("GPU MMU page table entry at 0x%08X is outside guest memory",
                              static_cast<uint32_t>(address));
    return cerf::le::U32(entry);
}

void AdrenoA2xxMemory::ReadBytes(uint64_t address, uint32_t size, const AdrenoA2xxMmu& mmu_config, uint8_t* out) {
    const AdrenoA2xxSpan span = ReadSpan(address, size, mmu_config);
    std::copy_n(span.data(), size, out);
}

void AdrenoA2xxMemory::CollectPages(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu_config,
                                    std::vector<GuestPageFences::Range>& data,
                                    std::vector<GuestPageFences::Range>& tables) {
    constexpr uint64_t kAddressSpace = uint64_t{1} << 32;
    if (size == 0u || address >= kAddressSpace) return;
    const uint64_t end = (std::min)(address + size, kAddressSpace);
    if (!Translates(address, mmu_config)) {
        data.push_back({static_cast<uint32_t>(address), static_cast<uint32_t>(end - address)});
        return;
    }
    auto append = [](std::vector<GuestPageFences::Range>& out, uint64_t pa, uint64_t bytes) {
        if (!out.empty() && uint64_t(out.back().pa) + out.back().bytes == pa &&
            uint64_t(out.back().bytes) + bytes <= UINT32_MAX)
            out.back().bytes += static_cast<uint32_t>(bytes);
        else
            out.push_back({static_cast<uint32_t>(pa), static_cast<uint32_t>(bytes)});
    };
    const uint64_t first = address >> kPageShift;
    const uint64_t last  = ((std::min)(end, VaEnd(mmu_config)) - 1u) >> kPageShift;
    const uint64_t table = uint64_t(mmu_config.pt_base) - (VaBase(mmu_config) >> kPageShift) * kPteBytes;
    for (uint64_t page = first; page <= last;) {
        const uint64_t entry_address = table + page * kPteBytes;
        const uint64_t run_end = (std::min)(last + 1u, page + (((entry_address | kPageOffset) + 1u - entry_address) /
                                                               kPteBytes));
        const uint64_t run_bytes = (run_end - page) * kPteBytes;
        if (entry_address + run_bytes > kAddressSpace) break;
        append(tables, entry_address, run_bytes);
        const uint8_t* entries = memory_->TryTranslateRange(entry_address, run_bytes, false);
        for (; page < run_end; ++page) {
            if (!entries) continue;
            const uint32_t value = cerf::le::U32(entries + (page - (entry_address - table) / kPteBytes) * kPteBytes);
            if ((value & (write ? kPteWrite : kPteRead)) == 0u) continue;
            const uint64_t begin = (std::max)(address, page << kPageShift);
            const uint64_t stop  = (std::min)(end, (page + 1u) << kPageShift);
            append(data, (value & kPteFrameMask) | (begin & kPageOffset), stop - begin);
        }
    }
}

uint32_t AdrenoA2xxMemory::ReadPa32(uint64_t address, const AdrenoA2xxMmu& mmu_config) {
    return cerf::le::U32(ReadSpan(address, 4u, mmu_config).data());
}
void AdrenoA2xxMemory::WritePa32(uint64_t address, uint32_t value, const AdrenoA2xxMmu& mmu_config) {
    AdrenoA2xxSpan span = WriteSpan(address, 4u, mmu_config);
    cerf::le::Put32(span.data(), value);
}

}
