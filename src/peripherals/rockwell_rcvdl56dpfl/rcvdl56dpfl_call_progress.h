#pragma once

#include "../phone_line/line_tone.h"
#include "rcvdl56dpfl_tone_detectors.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class PhoneLine;
class Rcvdl56dpflDspRam;
class Rcvdl56dpflLine;
class StateReader;
class StateWriter;

class Rcvdl56dpflCallProgress {
public:
    void Stop() { detectors_.Stop(); }

    void UpdateToneStatus(Rcvdl56dpflLine& line, const Rcvdl56dpflDspRam& ram, PhoneLine& phone, int64_t now_ns);
    void UpdateAnswerToneStatus(Rcvdl56dpflLine& line, PhoneLine& phone, int64_t now_ns);

    void SaveState(StateWriter& w) const { detectors_.SaveState(w); }
    void RestoreState(StateReader& r) { detectors_.RestoreState(r); }

private:
    static uint64_t SampleAt(int64_t ns);
    static uint64_t FirstSampleFrom(int64_t ns);
    static int64_t  SampleNs(uint64_t sample);

    Rcvdl56dpflToneDetectors detectors_;
    std::vector<LineTone>    line_tones_;
};

}
