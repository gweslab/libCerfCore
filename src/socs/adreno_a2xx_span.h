#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

class AdrenoA2xxSpan {
public:
    struct Segment {
        uint8_t* host;
        size_t   bytes;
    };

    AdrenoA2xxSpan() = default;
    AdrenoA2xxSpan(uint8_t* direct, size_t size);
    AdrenoA2xxSpan(std::vector<Segment> segments, size_t size, bool write);

    AdrenoA2xxSpan(AdrenoA2xxSpan&& other) noexcept;
    AdrenoA2xxSpan& operator=(AdrenoA2xxSpan&& other) noexcept;
    AdrenoA2xxSpan(const AdrenoA2xxSpan&)            = delete;
    AdrenoA2xxSpan& operator=(const AdrenoA2xxSpan&) = delete;
    ~AdrenoA2xxSpan();

    uint8_t*       data();
    const uint8_t* data() const;
    bool           Overlaps(const AdrenoA2xxSpan& other) const;

private:
    void                 Scatter();
    std::vector<Segment> Runs() const;

    uint8_t*             direct_ = nullptr;
    size_t               size_   = 0;
    std::vector<uint8_t> staging_;
    std::vector<Segment> segments_;
    bool                 write_ = false;
};

}
