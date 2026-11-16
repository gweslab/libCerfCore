#include "siemens_simatic_mobile_panels_gen2_fwf_fsf_container.h"

#include "../../boot/zlib_inflate.h"
#include "../../core/ascii_case.h"
#include "siemens_simatic_mobile_panels_gen2_flashboot.h"

#include <cstdio>
#include <cstring>
#include <string_view>
#include <cerfcore/core/byte_order.h>

namespace cerfcore {

namespace siemens_simatic_mobile_panels_gen2_fwf {
namespace {

constexpr uint8_t kFsfMagic[3] = {'F', 'S', 'F'};
constexpr uint16_t kVersion = 1u;
constexpr uint16_t kChunkedFlag = 2u;
constexpr size_t kMaxRecordBytes = 64u * 1024u * 1024u;

bool FsfError(std::string& error, const char* what, size_t at) {
    char text[96];
    std::snprintf(text, sizeof(text), "the FSF volume has a malformed %s at offset 0x%zX", what, at);
    error = text;
    return false;
}

}

bool ParseFsfVolume(const std::vector<uint8_t>& fwf, std::vector<FsfEntry>& out, std::string& error) {
    std::vector<uint8_t> volume;
    if (!siemens_simatic_mobile_panels_gen2_flashboot::ReadUpdateVolume(fwf, volume, error)) return false;
    const uint8_t* v = volume.data();
    if (volume.size() < 7u || std::memcmp(v, kFsfMagic, sizeof(kFsfMagic)) != 0) {
        error = "the FWF UpdateContainer does not hold an FSF volume";
        return false;
    }
    if (cerf::be::U16(v + 3u) != kVersion) return FsfError(error, "volume version", 3u);
    const size_t second = cerf::be::U16(v + 5u);
    if (second < 7u || second + 12u > volume.size() || cerf::be::U16(v + second) != kVersion)
        return FsfError(error, "second header block", second);
    const size_t second_len = cerf::be::U16(v + second + 2u);
    if (second_len < 12u || second + second_len > volume.size()) return FsfError(error, "second header block", second);
    const uint32_t declared_records = cerf::be::U32(v + second + 8u);

    out.clear();
    size_t off = second + second_len;
    while (out.size() < declared_records) {
        if (off + 15u > volume.size() || cerf::be::U16(v + off) != kVersion) return FsfError(error, "record header", off);
        const size_t header_len = cerf::be::U16(v + off + 2u);
        const uint16_t flags = cerf::be::U16(v + off + 4u);
        const uint8_t storage = v[off + 6u];
        const size_t first_len = cerf::be::U16(v + off + 7u);
        if (off + 11u + first_len > volume.size()) return FsfError(error, "record header", off);
        const size_t name_len = cerf::be::U16(v + off + 9u + first_len);
        if (name_len == 0u || header_len != 15u + first_len + name_len || off + header_len > volume.size())
            return FsfError(error, "record header", off);
        if ((flags & kChunkedFlag) != 0u) return FsfError(error, "chunked record, which is not modelled,", off);
        if (storage > 1u) return FsfError(error, "record storage byte", off);
        const std::string full(reinterpret_cast<const char*>(v + off + 11u + first_len), name_len);
        const uint32_t size = cerf::be::U32(v + off + 11u + first_len + name_len);
        const size_t data_at = off + header_len;

        std::string full_path = full;
        const std::string kRoot = "\\flash\\";
        if (full_path.size() > kRoot.size() && cerf::AsciiEqualIgnoreCase(std::string_view(full_path).substr(0, kRoot.size()), kRoot))
            full_path = full_path.substr(kRoot.size());
        const size_t slash = full_path.find_last_of('\\');
        FsfEntry entry;
        if (slash == std::string::npos) {
            entry.name = full_path;
        } else {
            entry.dir = full_path.substr(0, slash);
            entry.name = full_path.substr(slash + 1u);
        }

        if (storage == 0u) {
            if (size > volume.size() - data_at) return FsfError(error, "stored record data", off);
            entry.data.assign(volume.begin() + static_cast<ptrdiff_t>(data_at),
                              volume.begin() + static_cast<ptrdiff_t>(data_at + size));
            off = data_at + size;
        } else {
            if (size > kMaxRecordBytes) return FsfError(error, "compressed record size", off);
            size_t used = 0;
            entry.data = cerf::zlib_inflate::Decompress(v + data_at, volume.size() - data_at, size, &used);
            if (entry.data.size() != size || used == 0u) return FsfError(error, "compressed record data", off);
            off = data_at + used;
        }
        out.push_back(std::move(entry));
    }
    return true;
}

}

}
