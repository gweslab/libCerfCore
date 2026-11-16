#pragma once

#include "stm32f2_mcu.h"

#include <array>
#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Dma : public Peripheral {
public:
    static constexpr uint32_t kDma1Base = 0x40026000u;
    static constexpr uint32_t kDma2Base = 0x40026400u;
    static constexpr uint32_t kStreams = 8u;

    Dma(Mcu& mcu, uint32_t base) : mcu_(mcu), base_(base) {}

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;

    void SetRequest(uint32_t stream, uint32_t channel, bool asserted);

private:
    struct Stream {
        uint32_t cr = 0u;
        uint32_t ndtr = 0u;
        uint32_t par = 0u;
        uint32_t m0ar = 0u;
        uint32_t m1ar = 0u;
        uint32_t fcr = 0x21u;
        uint32_t initial_ndtr = 0u;
        uint32_t item = 0u;
    };

    bool Requested(uint32_t stream) const;
    void ServiceAll();
    void Transfer(uint32_t stream);
    void SetFlag(uint32_t stream, uint32_t flag);

    Mcu& mcu_;
    uint32_t base_;
    std::array<Stream, kStreams> streams_{};
    uint32_t lisr_ = 0u;
    uint32_t hisr_ = 0u;
    std::array<uint8_t, kStreams> requests_{};
    bool servicing_ = false;
};

}

}
