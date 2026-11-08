#pragma once

#include "../phone_line/line_tone.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class Rcvdl56dpflDspRam;
class StateReader;
class StateWriter;

class Rcvdl56dpflToneDetectors {
public:
    static constexpr uint8_t  kToneA        = 0x80u;
    static constexpr uint8_t  kToneB        = 0x40u;
    static constexpr uint8_t  kToneC        = 0x20u;
    static constexpr uint32_t kSampleRateHz = 7200u;

    struct DetectorCodes {
        uint32_t first;
        uint32_t second;
        uint32_t lpgain;
        uint32_t lpfbk;
        uint32_t threshu;
        uint32_t threshl;
    };

    void Start(uint64_t sample_index);
    void Stop();
    bool     Running() const { return running_; }
    uint64_t Sample() const { return sample_; }

    uint8_t Advance(const Rcvdl56dpflDspRam& ram, bool squarer, const std::vector<LineTone>& tones,
                    uint64_t to_sample);
    uint8_t Bits() const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    struct Biquad {
        double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;
    };
    struct Coefficients {
        double a1 = 0.0, a2 = 0.0, a3 = 0.0, b1 = 0.0, b2 = 0.0;
    };
    struct Detector {
        Biquad first;
        Biquad second;
        double level = 0.0;
        bool   on    = false;
    };
    struct DetectorSetup {
        Coefficients first;
        Coefficients second;
        double       lpgain;
        double       lpfbk;
        double       threshu;
        double       threshl;
    };
    struct Component {
        double freq_hz;
        double amplitude;
        double am_hz;
        double am_depth;
        double reversal_s;
        double origin_s;
    };

    double Synthesize(const Component& k) const;

    static Coefficients  BiquadAt(const Rcvdl56dpflDspRam& ram, uint32_t first_code);
    static DetectorSetup SetupAt(const Rcvdl56dpflDspRam& ram, const DetectorCodes& codes);
    static double        Step(Biquad& s, const Coefficients& c, double x);
    static double        Fraction(uint16_t word);
    static double        CalibratedAmplitude();
    static void          Detect(Detector& d, const DetectorSetup& s, double x);

    std::vector<Component> Components(const std::vector<LineTone>& tones) const;
    bool Quiescent() const;

    bool     running_     = false;
    uint64_t sample_      = 0u;
    double   calibration_ = 0.0;
    Detector a_;
    Detector b_;
    Biquad   pre_first_;
    Biquad   pre_second_;
    Detector c_;
};

}
