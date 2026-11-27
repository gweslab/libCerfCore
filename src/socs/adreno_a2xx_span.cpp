#include "adreno_a2xx_span.h"

#include <cstring>
#include <utility>

namespace cerfcore {

AdrenoA2xxSpan::AdrenoA2xxSpan(uint8_t* direct, size_t size) : direct_(direct), size_(size) {}

AdrenoA2xxSpan::AdrenoA2xxSpan(std::vector<Segment> segments, size_t size, bool write)
    : size_(size), staging_(size), segments_(std::move(segments)), write_(write) {
    size_t offset = 0u;
    for (const Segment& segment : segments_) {
        std::memcpy(staging_.data() + offset, segment.host, segment.bytes);
        offset += segment.bytes;
    }
}

AdrenoA2xxSpan::AdrenoA2xxSpan(AdrenoA2xxSpan&& other) noexcept
    : direct_(other.direct_), size_(other.size_), staging_(std::move(other.staging_)),
      segments_(std::move(other.segments_)), write_(other.write_) {
    other.direct_ = nullptr;
    other.size_   = 0u;
    other.segments_.clear();
}

AdrenoA2xxSpan& AdrenoA2xxSpan::operator=(AdrenoA2xxSpan&& other) noexcept {
    if (this != &other) {
        Scatter();
        direct_   = other.direct_;
        size_     = other.size_;
        staging_  = std::move(other.staging_);
        segments_ = std::move(other.segments_);
        write_    = other.write_;
        other.direct_ = nullptr;
        other.size_   = 0u;
        other.segments_.clear();
    }
    return *this;
}

AdrenoA2xxSpan::~AdrenoA2xxSpan() { Scatter(); }

void AdrenoA2xxSpan::Scatter() {
    if (write_) {
        size_t offset = 0u;
        for (const Segment& segment : segments_) {
            std::memcpy(segment.host, staging_.data() + offset, segment.bytes);
            offset += segment.bytes;
        }
    }
    segments_.clear();
}

uint8_t* AdrenoA2xxSpan::data() { return direct_ ? direct_ : staging_.data(); }

const uint8_t* AdrenoA2xxSpan::data() const { return direct_ ? direct_ : staging_.data(); }

std::vector<AdrenoA2xxSpan::Segment> AdrenoA2xxSpan::Runs() const {
    if (direct_) return {{direct_, size_}};
    return segments_;
}

bool AdrenoA2xxSpan::Overlaps(const AdrenoA2xxSpan& other) const {
    for (const Segment& a : Runs())
        for (const Segment& b : other.Runs())
            if (a.host < b.host + b.bytes && b.host < a.host + a.bytes) return true;
    return false;
}

}
