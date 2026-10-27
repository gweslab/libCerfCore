#include "dns_message.h"

#include <cerfcore/core/byte_order.h>

#include <algorithm>

namespace cerfcore {

namespace cerf::dns {

namespace {

using be::Put16;
using be::U16;

/* RFC 1035 4.1.4: labels ending in a zero octet, a pointer (top bits 11), or labels
   ending in a pointer. */
size_t SkipName(const uint8_t* msg, size_t len, size_t off) {
    while (off < len) {
        const uint8_t b = msg[off];
        if (b == 0) return off + 1;
        if ((b & 0xC0u) == 0xC0u) return off + 2 <= len ? off + 2 : 0;
        if ((b & 0xC0u) != 0) return 0;
        off += 1u + b;
    }
    return 0;
}

size_t SkipQuestion(const uint8_t* msg, size_t len, size_t off) {
    off = SkipName(msg, len, off);
    return off != 0 && off + kQuestionTailSize <= len ? off + kQuestionTailSize : 0;
}

size_t SkipRecord(const uint8_t* msg, size_t len, size_t off) {
    off = SkipName(msg, len, off);
    if (off == 0 || off + kRecordFixedSize > len) return 0;
    const size_t end = off + kRecordFixedSize + U16(msg, off + kRecordOffRdLength);
    return end <= len ? end : 0;
}

size_t QuestionsEnd(const uint8_t* msg, size_t len) {
    size_t off = kHeaderSize;
    for (unsigned i = U16(msg, kOffQdCount); i != 0; --i)
        if ((off = SkipQuestion(msg, len, off)) == 0) return 0;
    return off;
}

}

size_t SkipLabels(const uint8_t* msg, size_t len, size_t off) {
    while (off < len) {
        const uint8_t b = msg[off];
        if (b == 0) return off + 1;
        if ((b & 0xC0u) != 0) return 0;
        off += 1u + b;
    }
    return 0;
}

bool FindOpt(const uint8_t* msg, size_t len, RecordSpan& out) {
    if (len < kHeaderSize) return false;
    size_t off = QuestionsEnd(msg, len);
    if (off == 0) return false;
    for (unsigned i = U16(msg, kOffAnCount) + U16(msg, kOffNsCount); i != 0; --i)
        if ((off = SkipRecord(msg, len, off)) == 0) return false;
    for (unsigned i = U16(msg, kOffArCount); i != 0; --i) {
        const size_t end = SkipRecord(msg, len, off);
        if (end == 0) return false;
        if (U16(msg, SkipName(msg, len, off) + kRecordOffType) == kTypeOpt) {
            out = {off, end};
            return true;
        }
        off = end;
    }
    return false;
}

bool HasOpt(const uint8_t* msg, size_t len) {
    RecordSpan opt;
    return FindOpt(msg, len, opt);
}

size_t UdpAnswerLimit(const uint8_t* query, size_t len) {
    RecordSpan opt;
    if (!FindOpt(query, len, opt)) return kUdpPayloadLimit;
    return std::max<size_t>(kUdpPayloadLimit, U16(query, SkipName(query, len, opt.start) + kRecordOffClass));
}

std::vector<uint8_t> TruncatedAnswer(const uint8_t* answer, size_t len, bool with_opt) {
    if (len < kHeaderSize) return {};
    const size_t questions_end = QuestionsEnd(answer, len);
    if (questions_end == 0) return {};
    std::vector<uint8_t> out(answer, answer + questions_end);
    RecordSpan           opt;
    const bool           has_opt = with_opt && FindOpt(answer, len, opt);
    if (has_opt) out.insert(out.end(), answer + opt.start, answer + opt.end);
    Put16(out.data() + kOffFlags, static_cast<uint16_t>(U16(answer, kOffFlags) | kFlagTruncated));
    Put16(out.data() + kOffAnCount, 0);
    Put16(out.data() + kOffNsCount, 0);
    Put16(out.data() + kOffArCount, static_cast<uint16_t>(has_opt ? 1u : 0u));
    return out;
}

std::vector<uint8_t> EmptyAnswer(const uint8_t* query, size_t len, uint16_t rcode) {
    if (len < kHeaderSize) return {};
    const size_t         questions_end = QuestionsEnd(query, len);
    std::vector<uint8_t> out(query, query + (questions_end != 0 ? questions_end : kHeaderSize));
    const uint16_t       flags = U16(query, kOffFlags);
    Put16(out.data() + kOffFlags,
          static_cast<uint16_t>(kFlagResponse | (flags & (kFlagOpcodeMask | kFlagRecursionWant)) |
                                kFlagRecursionHave | rcode));
    Put16(out.data() + kOffQdCount, questions_end != 0 ? U16(query, kOffQdCount) : 0);
    Put16(out.data() + kOffAnCount, 0);
    Put16(out.data() + kOffNsCount, 0);
    RecordSpan opt;
    if (!FindOpt(query, len, opt)) {
        Put16(out.data() + kOffArCount, 0);
        return out;
    }
    const size_t   ttl      = SkipName(query, len, opt.start) + kRecordOffTtl;
    const uint32_t do_flag  = ((static_cast<uint32_t>(U16(query, ttl)) << 16) | U16(query, ttl + 2)) & kOptFlagDnssecOk;
    const size_t   at       = out.size();
    out.resize(at + 1 + kRecordFixedSize, 0);
    Put16(out.data() + at + 1 + kRecordOffType, kTypeOpt);
    Put16(out.data() + at + 1 + kRecordOffClass, static_cast<uint16_t>(kUdpPayloadLimit));
    Put16(out.data() + at + 1 + kRecordOffTtl + 2, static_cast<uint16_t>(do_flag));
    Put16(out.data() + kOffArCount, 1);
    return out;
}

}

}
