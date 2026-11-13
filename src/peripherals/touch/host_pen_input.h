#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace cerfengine { class HostRequestChannel; }

namespace cerfcore {

class CerfEmulator;

class HostPenInput {
public:
    struct Pen {
        uint8_t kind;
        int     x;
        int     y;
    };
    struct Point {
        int x;
        int y;
    };
    struct PanelRange {
        uint16_t x_lo;
        uint16_t x_hi;
        uint16_t y_lo;
        uint16_t y_hi;
    };
    static constexpr uint8_t kPenUp          = 0u;
    static constexpr uint8_t kPenDown        = 1u;
    static constexpr uint8_t kFirstOwnerKind = 2u;

    void Attach(CerfEmulator& emu, std::function<void(const Pen&)> apply);
    void Queue(const Pen& pen);
    void Track(int x, int y);
    Point Latest() const;
    void MapToPanel(int x, int y, const PanelRange& range, uint16_t* adc_x, uint16_t* adc_y) const;
    void Apply();
    void Clear();

private:
    CerfEmulator*                    emu_ = nullptr;
    std::function<void(const Pen&)>  apply_;
    std::mutex                       mtx_;
    std::vector<Pen>                 pens_;
    std::atomic<uint64_t>            latest_{0};
    cerfengine::HostRequestChannel*  requests_ = nullptr;
};

}
