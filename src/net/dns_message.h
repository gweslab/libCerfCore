#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace cerfcore {

namespace cerf::dns {

constexpr size_t   kHeaderSize        = 12;
constexpr size_t   kOffFlags          = 2;
constexpr size_t   kOffQdCount        = 4;
constexpr size_t   kOffAnCount        = 6;
constexpr size_t   kOffNsCount        = 8;
constexpr size_t   kOffArCount        = 10;
constexpr uint16_t kFlagResponse      = 0x8000;
constexpr uint16_t kFlagOpcodeMask    = 0x7800;
constexpr uint16_t kFlagTruncated     = 0x0200;
constexpr uint16_t kFlagRecursionWant = 0x0100;
constexpr uint16_t kFlagRecursionHave = 0x0080;
constexpr uint16_t kRcodeNoError      = 0;
constexpr uint16_t kRcodeFormErr      = 1;
constexpr uint16_t kRcodeServFail     = 2;
constexpr uint32_t kOptFlagDnssecOk   = 0x00008000;
constexpr size_t   kRecordOffType     = 0;
constexpr size_t   kRecordOffTtl      = 4;
constexpr size_t   kQuestionTailSize  = 4;
constexpr size_t   kRecordFixedSize   = 10;
constexpr size_t   kRecordOffClass    = 2;
constexpr size_t   kRecordOffRdLength = 8;
constexpr uint16_t kTypeAaaa          = 28;
constexpr uint16_t kTypeOpt           = 41;
constexpr size_t   kUdpPayloadLimit   = 512;
constexpr size_t   kMaxMessageSize    = 65535;

size_t SkipLabels(const uint8_t* msg, size_t len, size_t off);

struct RecordSpan {
    size_t start = 0;
    size_t end   = 0;
};

bool                 FindOpt(const uint8_t* msg, size_t len, RecordSpan& out);
bool                 HasOpt(const uint8_t* msg, size_t len);
size_t               UdpAnswerLimit(const uint8_t* query, size_t len);
std::vector<uint8_t> TruncatedAnswer(const uint8_t* answer, size_t len, bool with_opt);
std::vector<uint8_t> EmptyAnswer(const uint8_t* query, size_t len, uint16_t rcode);

}

}
