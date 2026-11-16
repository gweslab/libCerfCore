#pragma once

#include <cstddef>
#include <cstdint>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;

class Imx6FecLegacyRing {
public:
    explicit Imx6FecLegacyRing(CerfEmulator& emu) : emu_(emu) {}

    void Reset();
    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

    uint32_t Rdar() const { return rdar_; }
    uint32_t Tdar() const { return tdar_; }

    void SetRxDescriptorBase(uint32_t base);
    void SetTxDescriptorBase(uint32_t base);
    void Disable(uint32_t rx_base, uint32_t tx_base);
    void RequestReceive(bool controller_enabled);

    uint32_t RequestTransmit(bool controller_enabled, bool cable_connected, uint32_t tx_base,
                             uint32_t max_frame_length);
    uint32_t Receive(const uint8_t* frame, std::size_t length, uint32_t rx_base, uint32_t max_receive_buffer,
                     bool strip_crc, uint32_t wire_frame_length, uint32_t max_frame_length,
                     uint32_t truncation_length);

private:
    void RefreshReceiveDemand();

    uint8_t* RequireDescriptor(uint32_t pa);
    uint8_t* RequireBuffer(uint32_t pa, std::size_t bytes, bool write);

    CerfEmulator& emu_;
    uint32_t rdar_ = 0u;
    uint32_t tdar_ = 0u;
    uint32_t rx_descriptor_ = 0u;
    uint32_t tx_descriptor_ = 0u;
};

}
