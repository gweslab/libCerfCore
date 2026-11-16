#include "siemens_simatic_mobile_panels_gen2_f_module_firmware.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2 {

bool ParseContainerStructure(const std::uint8_t* data, std::size_t length,
                             ParsedContainer& parsed) noexcept {
    parsed = ParsedContainer{};
    if (data == nullptr || length < kUpdateHeaderBytes || length > kMaxUpdateContainerBytes) {
        return false;
    }
    if (!std::equal(kContainerIdentity.begin(), kContainerIdentity.end(), data)) {
        return false;
    }
    const std::size_t payload = cerf::le::U32(data + 0x20u);
    const std::size_t records = cerf::le::U32(data + 0x24u);
    if (payload == 0u || payload > kApplicationFlashBytes ||
        length != kUpdateHeaderBytes + records * kUpdateRecordBytes) {
        return false;
    }

    constexpr std::size_t kVersionAddress = 0x0803FF00u - 0x08000000u - kApplicationFlashOffset;
    const std::size_t version_offset = kUpdateHeaderBytes + (kVersionAddress / kUpdateRecordDataBytes) *
                                                                kUpdateRecordBytes +
                                       kVersionAddress % kUpdateRecordDataBytes;
    if (version_offset + kFirmwareVersionBytes > length ||
        std::memcmp(data + version_offset + 6u, "-FModule", 8u) != 0 || data[version_offset] != 'V') {
        return false;
    }

    parsed.payload_size = payload;
    return true;
}

bool ExtractApplication(const std::uint8_t* data, std::size_t length, std::vector<std::uint8_t>& image) {
    ParsedContainer parsed;
    if (!ParseContainerStructure(data, length, parsed)) return false;
    image.assign(parsed.payload_size, 0xFFu);
    for (std::size_t offset = 0u; offset < parsed.payload_size; offset += kUpdateRecordDataBytes) {
        const std::size_t record = kUpdateHeaderBytes + (offset / kUpdateRecordDataBytes) * kUpdateRecordBytes;
        const std::size_t count = std::min(kUpdateRecordDataBytes, parsed.payload_size - offset);
        if (record + count > length) return false;
        std::memcpy(image.data() + offset, data + record, count);
    }
    return true;
}

std::uint32_t Komp2Crc(const std::uint8_t* data, std::size_t length) {
    std::uint32_t crc = 0u;
    for (std::size_t i = 0u; i < length; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]) << 24;
        for (std::uint32_t bit = 0u; bit < 8u; ++bit) crc = (crc & 0x80000000u) != 0u ? (crc << 1) ^ 0xF4ACFB13u : crc << 1;
    }
    return crc;
}

}

}
