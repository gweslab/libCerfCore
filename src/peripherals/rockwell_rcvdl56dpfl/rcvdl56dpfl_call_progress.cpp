#include "rcvdl56dpfl_call_progress.h"

#include "../phone_line/phone_line.h"
#include "rcvdl56dpfl_configuration.h"
#include "rcvdl56dpfl_line.h"
#include "rcvdl56dpfl_registers.h"

#include <algorithm>

namespace cerfcore {

namespace {

using namespace rcvdl56dpfl_reg;

constexpr uint64_t kNsPerSecond = 1'000'000'000u;

}

uint64_t Rcvdl56dpflCallProgress::SampleAt(int64_t ns) {
    return static_cast<uint64_t>(ns) * Rcvdl56dpflToneDetectors::kSampleRateHz / kNsPerSecond;
}

uint64_t Rcvdl56dpflCallProgress::FirstSampleFrom(int64_t ns) {
    return (static_cast<uint64_t>(ns) * Rcvdl56dpflToneDetectors::kSampleRateHz + kNsPerSecond - 1u) / kNsPerSecond;
}

int64_t Rcvdl56dpflCallProgress::SampleNs(uint64_t sample) {
    return static_cast<int64_t>((sample * kNsPerSecond + Rcvdl56dpflToneDetectors::kSampleRateHz - 1u) /
                                Rcvdl56dpflToneDetectors::kSampleRateHz);
}

void Rcvdl56dpflCallProgress::UpdateToneStatus(Rcvdl56dpflLine& line, const Rcvdl56dpflDspRam& ram,
                                               PhoneLine& phone, int64_t now_ns) {
    const auto& regs = line.Regs();
    if (!(regs[kControl02] & kTde)) return;
    if (Rcvdl56dpflConfiguration::V8OrV34(line.ActiveConf())) {
        detectors_.Stop();
        line.SetStatus(kToneStatus, static_cast<uint8_t>(regs[kToneStatus] & ~kToneBits));
        return;
    }
    const uint64_t to = SampleAt(now_ns);
    phone.Advance(now_ns);
    if (!detectors_.Running()) detectors_.Start(to);
    const bool squarer = (regs[kControl02] & kSqdis) == 0u;
    while (detectors_.Sample() < to) {
        const int64_t  at   = SampleNs(detectors_.Sample());
        const int64_t  edge = phone.NextToneChangeNs(at);
        const uint64_t end  = edge == PhoneLine::kNoChange ? to : std::min(to, FirstSampleFrom(edge));
        phone.Tones(at, line_tones_);
        detectors_.Advance(ram, squarer, line_tones_, end);
    }
    line.SetStatus(kToneStatus, static_cast<uint8_t>((regs[kToneStatus] & ~kToneBits) | detectors_.Bits()));
}

void Rcvdl56dpflCallProgress::UpdateAnswerToneStatus(Rcvdl56dpflLine& line, PhoneLine& phone, int64_t now_ns) {
    const auto& regs     = line.Regs();
    const bool  detector = (regs[kMode] & kOrg) && !(regs[kMode] & kData) &&
                          Rcvdl56dpflConfiguration::ReportsV25AnswerTone(line.ActiveConf());
    phone.Advance(now_ns);
    const bool tone = detector && phone.AnswerToneAt(now_ns);
    line.SetStatus(kToneStatus, static_cast<uint8_t>((regs[kToneStatus] & ~kAtv25) | (tone ? kAtv25 : 0u)));
}

}
