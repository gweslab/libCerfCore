#include "udp_datagram.h"

#include "ipv4_packet.h"

#include <cerfcore/core/byte_order.h>

#include <cstring>

namespace cerfcore {

namespace cerf::inet {

namespace {

uint32_t PseudoHeaderSum(const uint8_t* ip, size_t udp_len) {
    return WordSum(ip + kIpOffSrc, 2 * kIpv4AddrSize) + static_cast<uint32_t>(udp_len) + kIpProtoUdp;
}

}

bool ParseUdpDatagram(const uint8_t* frame, size_t len, UdpDatagram& out) {
    if (len < kEthHeaderSize + kIpv4HeaderSize || EthType(frame) != kEthTypeIpv4) return false;
    const uint8_t* ip  = frame + kEthHeaderSize;
    const uint32_t ihl = Ipv4HeaderLen(ip);
    if ((ip[0] >> 4) != 4 || ihl < kIpv4HeaderSize || ip[kIpOffProto] != kIpProtoUdp) return false;
    if (kEthHeaderSize + ihl > len) return false;
    const uint16_t fragment = be::U16(ip, kIpOffFragment);
    if ((fragment & kIpFlagMoreFrags) != 0 || (fragment & kIpFragOffsetMask) != 0) return false;
    const size_t ip_end = kEthHeaderSize + be::U16(ip, kIpOffTotalLen);
    if (ip_end > len || ip_end < kEthHeaderSize + ihl + kUdpHeaderSize) return false;

    const uint8_t* udp     = ip + ihl;
    const size_t   udp_len = be::U16(udp, kUdpOffLen);
    if (udp_len < kUdpHeaderSize || kEthHeaderSize + ihl + udp_len > ip_end) return false;
    out.udp_off      = kEthHeaderSize + ihl;
    out.payload_off  = out.udp_off + kUdpHeaderSize;
    out.payload_size = udp_len - kUdpHeaderSize;
    out.src_port     = be::U16(udp, kUdpOffSrcPort);
    out.dst_port     = be::U16(udp, kUdpOffDstPort);
    return true;
}

bool UdpChecksumsValid(const uint8_t* frame, const UdpDatagram& datagram) {
    const uint8_t* ip      = frame + kEthHeaderSize;
    const uint8_t* udp     = frame + datagram.udp_off;
    const size_t   udp_len = kUdpHeaderSize + datagram.payload_size;
    if (InetChecksum(ip, datagram.udp_off - kEthHeaderSize) != 0) return false;
    return be::U16(udp, kUdpOffChecksum) == 0 || InetChecksum(udp, udp_len, PseudoHeaderSum(ip, udp_len)) == 0;
}

std::vector<uint8_t> BuildUdpReply(const uint8_t* query_frame, const UdpDatagram& query,
                                   const uint8_t* payload, size_t size) {
    const size_t udp_size = kUdpHeaderSize + size;
    if (kIpv4HeaderSize + udp_size > 0xFFFFu) return {};

    std::vector<uint8_t> out(kEthHeaderSize + kIpv4HeaderSize + udp_size, 0);
    PutEthHeader(out.data(), query_frame + kEthOffSrc, query_frame + kEthOffDst, kEthTypeIpv4);
    const uint8_t* query_ip = query_frame + kEthHeaderSize;
    uint8_t*       ip       = out.data() + kEthHeaderSize;
    WriteIpv4Header(ip, static_cast<uint16_t>(kIpv4HeaderSize + udp_size), kIpProtoUdp, query_ip + kIpOffDst,
                    query_ip + kIpOffSrc);

    uint8_t* udp = ip + kIpv4HeaderSize;
    be::Put16(udp + kUdpOffSrcPort, query.dst_port);
    be::Put16(udp + kUdpOffDstPort, query.src_port);
    be::Put16(udp + kUdpOffLen, static_cast<uint16_t>(udp_size));
    if (size != 0) std::memcpy(udp + kUdpHeaderSize, payload, size);
    uint16_t checksum = InetChecksum(udp, udp_size, PseudoHeaderSum(ip, udp_size));
    if (checksum == 0) checksum = 0xFFFFu;
    be::Put16(udp + kUdpOffChecksum, checksum);
    return out;
}

}

}
