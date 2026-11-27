#pragma once
#include "adreno_a2xx_mmu.h"
#include "adreno_a2xx_span.h"
#include "../cpu/guest_page_fences.h"
#include <cerfcore/core/service.h>
#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

class EmulatedMemory;

struct AdrenoA2xxPageCache {
    static constexpr uint32_t kEntries = 64;
    struct Entry {
        uint64_t       page       = 0;
        uint8_t*       host       = nullptr;
        uint64_t       generation = 0;
    };
    std::array<Entry, kEntries> entries{};
    uint64_t                    generation = 1;
    void Reset() { ++generation; }
};

class AdrenoA2xxMemory : public Service {
public:
    using Service::Service;
    bool ShouldRegister() override;
    void OnReady() override;
    AdrenoA2xxSpan ReadSpan(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config);
    AdrenoA2xxSpan WriteSpan(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config);
    const uint8_t* UntranslatedRead(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config);
    const uint8_t* CachedRead(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size,
                              const AdrenoA2xxMmu& mmu_config);
    uint8_t*       CachedWrite(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size,
                               const AdrenoA2xxMmu& mmu_config);
    uint8_t*       CachedOrSpanWrite(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size,
                                     const AdrenoA2xxMmu& mmu_config, AdrenoA2xxSpan& fallback);
    uint8_t*       UntranslatedWrite(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config);
    void           RequireReadable(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config);
    void           RequireWritable(uint64_t address, uint64_t size, const AdrenoA2xxMmu& mmu_config);
    void     ReadBytes(uint64_t address, uint32_t size, const AdrenoA2xxMmu& mmu_config, uint8_t* out);
    void     CollectPages(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu_config,
                          std::vector<GuestPageFences::Range>& data, std::vector<GuestPageFences::Range>& tables);
    uint32_t ReadPa32(uint64_t address, const AdrenoA2xxMmu& mmu_config);
    void WritePa32(uint64_t address, uint32_t value, const AdrenoA2xxMmu& mmu_config);

private:
    AdrenoA2xxSpan MemorySpan(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu);
    uint8_t*       PageRuns(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu,
                            std::vector<AdrenoA2xxSpan::Segment>* runs);
    bool           Translates(uint64_t address, const AdrenoA2xxMmu& mmu) const;
    uint8_t*       Untranslated(uint64_t address, uint64_t size, bool write, const AdrenoA2xxMmu& mmu);
    uint8_t*       HostRange(uint64_t physical, uint64_t size, bool write, const AdrenoA2xxMmu& mmu);
    uint8_t*       Translate(uint64_t physical, uint64_t size, bool write, const AdrenoA2xxMmu& mmu);
    uint8_t*       CachedPage(AdrenoA2xxPageCache& cache, uint64_t address, uint32_t size, bool write,
                              const AdrenoA2xxMmu& mmu);
    uint64_t       PagePhysical(uint64_t page, bool write, const AdrenoA2xxMmu& mmu);
    uint32_t       ReadPageTableEntry(uint64_t index, const AdrenoA2xxMmu& mmu);

    EmulatedMemory* memory_ = nullptr;
};

}
