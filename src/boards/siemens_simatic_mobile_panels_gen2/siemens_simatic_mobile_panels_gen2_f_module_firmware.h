#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

inline constexpr std::size_t kApplicationFlashOffset = 0x10000u;
inline constexpr std::size_t kApplicationFlashBytes = 0x30000u;
inline constexpr std::size_t kUpdateHeaderBytes = 0x88u;
inline constexpr std::size_t kUpdateRecordBytes = 0x88u;
inline constexpr std::size_t kUpdateRecordDataBytes = 0x80u;
inline constexpr std::size_t kMaxUpdateContainerBytes =
    kUpdateHeaderBytes + (kApplicationFlashBytes / kUpdateRecordDataBytes) * kUpdateRecordBytes;
inline constexpr std::size_t kFirmwareVersionBytes = 20;

inline constexpr std::array<std::uint8_t, 18> kContainerIdentity{{
    'K', 'T', 'P', '_', 'M', 'O', 'B', 'I', 'L', 'E',
    '_', 'F', 'M', 'O', 'D', 'U', 'L', 'E'
}};

struct ParsedContainer {
    std::size_t payload_size = 0u;
};

bool ParseContainerStructure(const std::uint8_t* data, std::size_t length,
                             ParsedContainer& parsed) noexcept;

bool ExtractApplication(const std::uint8_t* data, std::size_t length, std::vector<std::uint8_t>& image);

std::uint32_t Komp2Crc(const std::uint8_t* data, std::size_t length);

}

}
