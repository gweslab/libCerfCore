#include "compaq_ipaq_h3000_micro_frame.h"

namespace cerfcore {

size_t BuildCompaqIpaqH3000MicroFrame(uint8_t msg_id, const uint8_t* data, uint8_t len,
                                      uint8_t* out) {
    const uint8_t hdr = static_cast<uint8_t>(((msg_id & 0x0Fu) << 4) | (len & 0x0Fu));
    uint8_t cksum = hdr;
    size_t  n     = 0;
    out[n++] = kCompaqIpaqH3000MicroSof;
    out[n++] = hdr;
    for (uint8_t i = 0; i < len; ++i) {
        out[n++] = data[i];
        cksum    = static_cast<uint8_t>(cksum + data[i]);
    }
    out[n++] = cksum;
    return n;
}

}
