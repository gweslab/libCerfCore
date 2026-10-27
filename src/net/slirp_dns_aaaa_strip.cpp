#include "slirp_backend.h"

#include <vector>

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/log.h>
#include "dns_message.h"
#include "ipv4_packet.h"
#include "udp_datagram.h"

namespace cerfcore {

namespace {

using cerf::be::U16;
using namespace cerf::inet;
namespace dns = cerf::dns;

bool IsAaaaQuestion(const uint8_t* msg, size_t len) {
    if (len < dns::kHeaderSize) return false;
    if ((U16(msg, dns::kOffFlags) & dns::kFlagResponse) != 0) return false;
    if (U16(msg, dns::kOffQdCount) != 1) return false;
    const size_t name_end = dns::SkipLabels(msg, len, dns::kHeaderSize);
    if (name_end == 0 || name_end + dns::kQuestionTailSize > len) return false;
    return U16(msg, name_end) == dns::kTypeAaaa;
}

}

bool SlirpBackend::TryInterceptAaaaQuery(const uint8_t* frame, std::size_t len) {
    if (host_has_v6_) return false;

    UdpDatagram d;
    if (!ParseUdpDatagram(frame, len, d) || d.dst_port != kUdpPortDns) return false;
    if (!UdpChecksumsValid(frame, d)) return false;
    const uint8_t* query = frame + d.payload_off;
    if (!IsAaaaQuestion(query, d.payload_size)) return false;

    const std::vector<uint8_t> answer = dns::EmptyAnswer(query, d.payload_size, dns::kRcodeNoError);
    const std::vector<uint8_t> reply  = BuildUdpReply(frame, d, answer.data(), answer.size());
    if (reply.empty()) return false;

    DispatchFrame(reply.data(), reply.size());

    LOG(Net, "AAAA NoData synthesized (host has no v6 internet; "
             "%u-byte query → %zu-byte reply)\n",
        (unsigned)len, reply.size());
    return true;
}

}
