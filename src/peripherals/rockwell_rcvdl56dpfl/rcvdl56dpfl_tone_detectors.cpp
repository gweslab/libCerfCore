#include "rcvdl56dpfl_tone_detectors.h"

#include "rcvdl56dpfl_dsp_ram.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace cerfcore {

namespace {

constexpr uint32_t kPrefilterFirst  = 0xAB2u;
constexpr uint32_t kPrefilterSecond = 0xBB2u;

constexpr uint32_t kA3 = 0u;
constexpr uint32_t kA2 = 1u;
constexpr uint32_t kA1 = 2u;
constexpr uint32_t kB2 = 3u;
constexpr uint32_t kB1 = 4u;

constexpr double kNsPerSecond         = 1e9;
constexpr double kCalibrationHz       = 500.0;
constexpr double kCalibrationLevelDbm = -35.0;
constexpr double kAgcCutoffDbm        = -26.0;

}

void Rcvdl56dpflToneDetectors::Start(uint64_t sample_index) {
    *this        = Rcvdl56dpflToneDetectors{};
    running_     = true;
    sample_      = sample_index;
    calibration_ = CalibratedAmplitude();
}

void Rcvdl56dpflToneDetectors::Stop() {
    *this = Rcvdl56dpflToneDetectors{};
}

double Rcvdl56dpflToneDetectors::Fraction(uint16_t word) {
    return static_cast<int16_t>(word) / 32768.0;
}

Rcvdl56dpflToneDetectors::Coefficients Rcvdl56dpflToneDetectors::BiquadAt(const Rcvdl56dpflDspRam& ram,
                                                                          uint32_t first_code) {
    return {Fraction(ram.Read(first_code + kA1)), Fraction(ram.Read(first_code + kA2)),
            Fraction(ram.Read(first_code + kA3)), Fraction(ram.Read(first_code + kB1)),
            Fraction(ram.Read(first_code + kB2))};
}

Rcvdl56dpflToneDetectors::DetectorSetup Rcvdl56dpflToneDetectors::SetupAt(const Rcvdl56dpflDspRam& ram,
                                                                          const DetectorCodes& codes) {
    return {BiquadAt(ram, codes.first), BiquadAt(ram, codes.second), Fraction(ram.Read(codes.lpgain)),
            Fraction(ram.Read(codes.lpfbk)), Fraction(ram.Read(codes.threshu)), Fraction(ram.Read(codes.threshl))};
}

double Rcvdl56dpflToneDetectors::Step(Biquad& s, const Coefficients& c, double x) {
    const double y = 2.0 * (c.a1 * x + c.a2 * s.x1 + c.a3 * s.x2) + 2.0 * c.b1 * s.y1 + 2.0 * c.b2 * s.y2;
    s.x2 = s.x1;
    s.x1 = x;
    s.y2 = s.y1;
    s.y1 = y;
    return y;
}

constexpr Rcvdl56dpflToneDetectors::DetectorCodes kToneACodes{0xAA1u, 0xBA1u, 0xAA0u, 0xBA0u, 0xAB8u, 0xBB8u};
constexpr Rcvdl56dpflToneDetectors::DetectorCodes kToneBCodes{0xAA7u, 0xBA7u, 0xAA6u, 0xBA6u, 0xAB9u, 0xBB9u};
constexpr Rcvdl56dpflToneDetectors::DetectorCodes kToneCCodes{0xAADu, 0xBADu, 0xAACu, 0xBACu, 0xABAu, 0xBBAu};

double Rcvdl56dpflToneDetectors::CalibratedAmplitude() {
    Rcvdl56dpflDspRam ram;
    ram.PowerOn();
    const DetectorSetup s = SetupAt(ram, kToneACodes);
    const double w = 2.0 * std::numbers::pi * kCalibrationHz / kSampleRateHz;
    const std::complex<double> z1 = std::polar(1.0, -w);
    const std::complex<double> z2 = z1 * z1;
    const auto gain = [&](const Coefficients& c) {
        return std::abs(2.0 * (c.a1 + c.a2 * z1 + c.a3 * z2) / (1.0 - 2.0 * c.b1 * z1 - 2.0 * c.b2 * z2));
    };
    const double smoothing = s.lpgain / (1.0 - s.lpfbk);
    return s.threshu / (smoothing * (2.0 / std::numbers::pi) * gain(s.first) * gain(s.second));
}

std::vector<Rcvdl56dpflToneDetectors::Component> Rcvdl56dpflToneDetectors::Components(
    const std::vector<LineTone>& tones) const {
    std::vector<Component> out;
    if (tones.empty()) return out;
    double total_mw = 0.0;
    for (const LineTone& t : tones) total_mw += std::pow(10.0, t.level_dbm / 10.0);
    const double agc = std::min(1.0, std::pow(10.0, (kAgcCutoffDbm - 10.0 * std::log10(total_mw)) / 20.0));
    for (const LineTone& t : tones)
        out.push_back({t.freq_hz, calibration_ * std::pow(10.0, (t.level_dbm - kCalibrationLevelDbm) / 20.0) * agc,
                       t.am_hz, t.am_depth, static_cast<double>(t.reversal_ns) / kNsPerSecond,
                       static_cast<double>(t.origin_ns) / kNsPerSecond});
    return out;
}

double Rcvdl56dpflToneDetectors::Synthesize(const Component& k) const {
    const double cycles = std::fmod(k.freq_hz * static_cast<double>(sample_), static_cast<double>(kSampleRateHz));
    double       phase  = 2.0 * std::numbers::pi * cycles / kSampleRateHz;
    const double since  = static_cast<double>(sample_) / kSampleRateHz - k.origin_s;
    if (k.reversal_s > 0.0 && since > 0.0 && static_cast<uint64_t>(since / k.reversal_s) % 2u != 0u)
        phase += std::numbers::pi;
    double envelope = 1.0;
    if (k.am_hz > 0.0) envelope += k.am_depth * std::sin(2.0 * std::numbers::pi * k.am_hz * since);
    return k.amplitude * envelope * std::sin(phase);
}

void Rcvdl56dpflToneDetectors::Detect(Detector& d, const DetectorSetup& s, double x) {
    const double y = Step(d.second, s.second, Step(d.first, s.first, x));
    d.level = s.lpgain * std::abs(y) + s.lpfbk * d.level;
    if (!d.on && d.level > s.threshu)
        d.on = true;
    else if (d.on && d.level < s.threshl)
        d.on = false;
}

bool Rcvdl56dpflToneDetectors::Quiescent() const {
    const auto rest = [](const Biquad& s) { return s.x1 == 0.0 && s.x2 == 0.0 && s.y1 == 0.0 && s.y2 == 0.0; };
    const auto idle = [&](const Detector& d) { return rest(d.first) && rest(d.second) && d.level == 0.0 && !d.on; };
    return idle(a_) && idle(b_) && idle(c_) && rest(pre_first_) && rest(pre_second_);
}

uint8_t Rcvdl56dpflToneDetectors::Advance(const Rcvdl56dpflDspRam& ram, bool squarer,
                                          const std::vector<LineTone>& tones, uint64_t to_sample) {
    const std::vector<Component> components = Components(tones);
    const DetectorSetup a = SetupAt(ram, kToneACodes);
    const DetectorSetup b = SetupAt(ram, kToneBCodes);
    const DetectorSetup c = SetupAt(ram, kToneCCodes);
    const Coefficients  pre1 = BiquadAt(ram, kPrefilterFirst);
    const Coefficients  pre2 = BiquadAt(ram, kPrefilterSecond);
    while (sample_ < to_sample) {
        if (components.empty() && Quiescent()) {
            sample_ = to_sample;
            break;
        }
        double x = 0.0;
        for (const Component& k : components) x += Synthesize(k);
        Detect(a_, a, x);
        Detect(b_, b, x);
        double p = Step(pre_second_, pre2, Step(pre_first_, pre1, x));
        if (squarer) p *= p;
        Detect(c_, c, p);
        ++sample_;
    }
    return Bits();
}

uint8_t Rcvdl56dpflToneDetectors::Bits() const {
    return static_cast<uint8_t>((a_.on ? kToneA : 0u) | (b_.on ? kToneB : 0u) | (c_.on ? kToneC : 0u));
}

void Rcvdl56dpflToneDetectors::SaveState(StateWriter& w) const {
    const auto biquad = [&](const Biquad& s) {
        w.Write("tone_x1", s.x1);
        w.Write("tone_x2", s.x2);
        w.Write("tone_y1", s.y1);
        w.Write("tone_y2", s.y2);
    };
    w.Write<uint8_t>("tone_running", running_ ? 1u : 0u);
    w.Write<uint64_t>("tone_sample", sample_);
    for (const Detector* d : {&a_, &b_, &c_}) {
        biquad(d->first);
        biquad(d->second);
        w.Write("tone_level", d->level);
        w.Write<uint8_t>("tone_on", d->on ? 1u : 0u);
    }
    biquad(pre_first_);
    biquad(pre_second_);
}

void Rcvdl56dpflToneDetectors::RestoreState(StateReader& r) {
    const auto biquad = [&](Biquad& s) {
        r.Read("tone_x1", s.x1);
        r.Read("tone_x2", s.x2);
        r.Read("tone_y1", s.y1);
        r.Read("tone_y2", s.y2);
    };
    uint8_t running = 0u;
    r.Read("tone_running", running);
    running_ = running != 0u;
    r.Read("tone_sample", sample_);
    for (Detector* d : {&a_, &b_, &c_}) {
        biquad(d->first);
        biquad(d->second);
        r.Read("tone_level", d->level);
        uint8_t on = 0u;
        r.Read("tone_on", on);
        d->on = on != 0u;
    }
    biquad(pre_first_);
    biquad(pre_second_);
    calibration_ = running_ ? CalibratedAmplitude() : 0.0;
}

}
