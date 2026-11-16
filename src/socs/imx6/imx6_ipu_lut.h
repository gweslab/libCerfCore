#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>

namespace cerfcore {

constexpr uint32_t kIpuLutCpuOffsetStub = 0x00020000u;

class Imx6IpuLut : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    bool ContainsOffset(uint32_t offset) const;
    uint8_t ReadByte(uint32_t offset) const;
    uint16_t ReadHalf(uint32_t offset) const;
    uint32_t ReadWord(uint32_t offset) const;
    void WriteByte(uint32_t offset, uint8_t value);
    void WriteHalf(uint32_t offset, uint16_t value);
    void WriteWord(uint32_t offset, uint32_t value);
    void Reset();
    uint32_t Decode(uint32_t pixel, uint32_t dec_sel, bool four_bit) const;
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);

private:
    std::array<uint32_t, 256> pixels_{};
};

}
