#pragma once

#include "msm8255_mdp_pixel.h"

#include <cerfcore/core/service.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Msm8255MdpHistogram : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static bool Owns(uint32_t off);
    uint32_t Read(uint32_t off);
    void Write(uint32_t off, uint32_t value);
    void CountFrame(const Msm8255MdpPixel* pixels, size_t count);
    bool TakeInterrupt();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    static constexpr uint32_t kBins     = 32u;
    static constexpr uint32_t kChannels = 3u;

    void Raise(uint32_t bits);
    void Reset();
    bool BinSlot(uint32_t off, uint32_t& channel, uint32_t& bin) const;
    [[noreturn]] void Halt(const char* access, uint32_t off, uint32_t value);

    uint32_t frame_count_ = 0u;
    uint32_t word10_      = 0u;
    uint32_t status_      = 0u;
    uint32_t enable_      = 0u;
    uint32_t frames_left_ = 0u;
    bool     interrupt_   = false;
    std::array<std::array<uint32_t, kBins>, kChannels> bins_{};
};

}
