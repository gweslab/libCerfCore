#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

class BlockReadOverlay {
public:
    explicit BlockReadOverlay(uint32_t block_bytes) : block_bytes_(block_bytes) {}

    void           Set(uint64_t block, const uint8_t* bytes);
    const uint8_t* Find(uint64_t block) const;
    void           Drop(uint64_t block);

    void SaveState(StateWriter& w, const char* count_field, const char* block_field) const;
    void RestoreState(StateReader& r, const char* count_field, const char* block_field);

private:
    uint32_t                                           block_bytes_;
    std::unordered_map<uint64_t, std::vector<uint8_t>> blocks_;
    std::unordered_set<uint64_t>                       dropped_;
};

}
