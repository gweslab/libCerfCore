#include "block_read_overlay.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>

namespace cerfcore {

void BlockReadOverlay::Set(uint64_t block, const uint8_t* bytes) {
    dropped_.erase(block);
    blocks_[block].assign(bytes, bytes + block_bytes_);
}

const uint8_t* BlockReadOverlay::Find(uint64_t block) const {
    const auto it = blocks_.find(block);
    if (it == blocks_.end() || dropped_.contains(block)) return nullptr;
    return it->second.data();
}

void BlockReadOverlay::Drop(uint64_t block) {
    if (blocks_.contains(block)) dropped_.insert(block);
}

void BlockReadOverlay::SaveState(StateWriter& w, const char* count_field,
                                 const char* block_field) const {
    std::vector<uint64_t> kept;
    kept.reserve(blocks_.size());
    for (const auto& [block, bytes] : blocks_)
        if (!dropped_.contains(block)) kept.push_back(block);
    std::sort(kept.begin(), kept.end());
    w.Write<uint64_t>(count_field, kept.size());
    for (uint64_t block : kept) w.Write(block_field, block);
}

void BlockReadOverlay::RestoreState(StateReader& r, const char* count_field,
                                    const char* block_field) {
    uint64_t count = 0;
    r.Read(count_field, count);
    std::unordered_set<uint64_t> kept;
    for (uint64_t i = 0; i < count; ++i) {
        uint64_t block = 0;
        r.Read(block_field, block);
        kept.insert(block);
    }
    dropped_.clear();
    for (const auto& [block, bytes] : blocks_)
        if (!kept.contains(block)) dropped_.insert(block);
}

}
