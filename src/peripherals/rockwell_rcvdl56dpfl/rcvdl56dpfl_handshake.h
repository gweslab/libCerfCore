#pragma once

#include <cstdint>

namespace cerfcore {

class PhoneLine;
struct Rcvdl56dpflCleardown;
class Rcvdl56dpflDspRam;
class Rcvdl56dpflLine;
class StateReader;
class StateWriter;

class Rcvdl56dpflHandshake {
public:
    void Reset();
    bool Active() const { return state_ != State::kIdle; }
    bool Connected() const { return state_ == State::kConnected; }

    const char* OnNewConfiguration(Rcvdl56dpflLine& line, PhoneLine& phone);
    void        FollowControls(Rcvdl56dpflLine& line, PhoneLine& phone);
    const char* Advance(Rcvdl56dpflLine& line, Rcvdl56dpflDspRam& ram, PhoneLine& phone, int64_t now_ns);

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    enum class State : uint8_t { kIdle, kOriginating, kConnected, kClearedDown };

    const char* Connect(Rcvdl56dpflLine& line, Rcvdl56dpflDspRam& ram, PhoneLine& phone);
    const char* Refuse(const char* format, unsigned value);
    void        ClearDown(Rcvdl56dpflLine& line, PhoneLine& phone, const Rcvdl56dpflCleardown& cleardown);

    State state_ = State::kIdle;
    char  reason_[32] = {};
};

}
