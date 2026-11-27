#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace cerfcore {

inline constexpr uint32_t kBq27520DataFlashRows        = 32u;
inline constexpr uint32_t kBq27520DataFlashRowBytes    = 32u;
inline constexpr uint32_t kBq27520DataFlashBytes       = kBq27520DataFlashRows * kBq27520DataFlashRowBytes;
inline constexpr uint32_t kBq27520ManufacturerInfoBase = 0x2BCu;
inline constexpr uint32_t kBq27520ManufacturerInfoSize = 32u;
inline constexpr uint32_t kBq27520ItEnableOffset       = 0x81u;
inline constexpr uint32_t kBq27520RegistersBase        = 0x229u;
inline constexpr uint32_t kBq27520DischargeBase        = 0x2A3u;
inline constexpr uint32_t kBq27520SignatureBytes       = 4u;

class TiBq27520Flash : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    uint8_t  ReadDataFlash(uint32_t offset) const;
    void     WriteDataFlashByte(uint32_t offset, uint8_t value);
    uint16_t DataFlashSum() const;
    void     EraseDataFlash();
    void     ProgramDataFlashRow(uint32_t row, std::span<const uint8_t, kBq27520DataFlashRowBytes> data);

    void EraseInstructionRow(uint32_t row);
    void ProgramInstructionRow(uint32_t row);
    bool FirmwareValid() const { return !instruction_row0_erased_; }

    std::array<uint8_t, kBq27520SignatureBytes> FirmwareSignature() const;

private:
    enum class ByteState : uint8_t { kUnknown = 0u, kErased = 1u, kKnown = 2u };

    void Load(const std::vector<uint8_t>& bytes);
    void Persist() const;

    std::string                                   path_;
    std::array<uint8_t, kBq27520DataFlashBytes>   data_{};
    std::array<ByteState, kBq27520DataFlashBytes> state_{};
    bool                                          instruction_row0_erased_ = false;
};

}
