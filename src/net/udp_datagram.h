#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace cerf::inet {

struct UdpDatagram {
    size_t   udp_off      = 0;
    size_t   payload_off  = 0;
    size_t   payload_size = 0;
    uint16_t src_port     = 0;
    uint16_t dst_port     = 0;
};

bool ParseUdpDatagram(const uint8_t* frame, size_t len, UdpDatagram& out);

bool UdpChecksumsValid(const uint8_t* frame, const UdpDatagram& datagram);

std::vector<uint8_t> BuildUdpReply(const uint8_t* query_frame, const UdpDatagram& query,
                                   const uint8_t* payload, size_t size);

}

}
