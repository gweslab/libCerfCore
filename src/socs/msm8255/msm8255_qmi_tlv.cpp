#include "msm8255_qmi_tlv.h"

#include <cerfcore/core/byte_order.h>

namespace cerfcore {

void AppendMsm8255QmiTlv(std::vector<uint8_t>& out, uint8_t type, const std::vector<uint8_t>& value) {
    out.push_back(type);
    cerf::le::Append16(out, static_cast<uint16_t>(value.size()));
    out.insert(out.end(), value.begin(), value.end());
}

bool ReadMsm8255QmiTlv(const uint8_t* bytes, uint32_t size, uint32_t offset, Msm8255QmiTlv& tlv) {
    if (offset > size || size - offset < kMsm8255QmiTlvHeaderBytes) return false;
    tlv.type   = bytes[offset];
    tlv.length = cerf::le::U16(bytes, offset + 1u);
    tlv.value  = offset + kMsm8255QmiTlvHeaderBytes;
    return size - tlv.value >= tlv.length;
}

}
