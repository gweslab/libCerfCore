#pragma once

#include "stm32f2_mcu.h"

#include <cstdint>

namespace cerfcore {

namespace stm32f2 {

class Dma;
class Rcc;

class UsartLine {
public:
    virtual ~UsartLine() = default;
    virtual void Deliver(uint8_t value) = 0;
};

class Usart : public Peripheral {
public:
    Usart(Mcu& mcu, Rcc& rcc, uint32_t base, bool apb2, Dma& dma, uint32_t rx_stream, uint32_t tx_stream,
          uint32_t channel)
        : mcu_(mcu), rcc_(rcc), base_(base), apb2_(apb2), dma_(dma), rx_stream_(rx_stream),
          tx_stream_(tx_stream), channel_(channel) {}

    void ConnectTx(UsartLine* line) { tx_line_ = line; }

    uint32_t Read(uint32_t offset, uint32_t size) override;
    void Write(uint32_t offset, uint32_t size, uint32_t value) override;
    void SaveState(StateWriter& writer) const override;
    void RestoreState(StateReader& reader) override;
    void Tick(uint32_t hclk_cycles) override;
    uint64_t CyclesToEvent() const override { return shifting_ ? shift_remaining_ : UINT64_MAX; }
    void ClockChanged() override;

    void Receive(uint8_t value, const Usart& sender);
    bool TxLevel(bool* level) const;

private:
    uint64_t FrameHclkCycles() const;
    uint64_t BitFckCycles() const;
    void StartShift();
    void StartIdleFrame();
    void UpdateRequests();

    Mcu& mcu_;
    Rcc& rcc_;
    uint32_t base_;
    bool apb2_;
    Dma& dma_;
    uint32_t rx_stream_;
    uint32_t tx_stream_;
    uint32_t channel_;
    UsartLine* tx_line_ = nullptr;
    uint32_t sr_ = 0x00C0u;
    uint32_t brr_ = 0u;
    uint32_t cr1_ = 0u;
    uint32_t cr2_ = 0u;
    uint32_t cr3_ = 0u;
    uint32_t tdr_ = 0u;
    uint32_t rdr_ = 0u;
    uint32_t shift_ = 0u;
    bool shifting_ = false;
    bool idle_frame_ = false;
    bool sr_read_ = false;
    uint64_t shift_remaining_ = 0u;
    bool idle_armed_ = false;
    uint64_t idle_remaining_ = 0u;
};

}

}
