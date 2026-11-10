#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

class Sh77xxIntc;
class Sh77xxPortData;

class Hp600Sh7709KeyMatrix : public Service {
public:
    using Service::Service;

    static constexpr uint32_t kRows    = 8u;
    static constexpr uint32_t kColumns = 16u;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetKey(uint32_t cell, bool down);
    void TrackRows(const Sh77xxPortData& ports);

private:
    void PublishLocked();

    std::mutex  mtx_;
    Sh77xxIntc* intc_            = nullptr;
    uint16_t    pressed_[kRows]  = {};
    uint8_t     rows_low_        = 0u;
};

}
