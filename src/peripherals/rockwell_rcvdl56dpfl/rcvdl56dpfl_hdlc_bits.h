#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

inline constexpr uint8_t kRcvdl56dpflHdlcFlag = 0x7Eu;

class Rcvdl56dpflZeroInsertion {
public:
    template <typename Emit>
    void Byte(uint8_t value, Emit&& emit) {
        for (uint8_t i = 0; i < 8u; ++i) {
            const uint8_t bit = static_cast<uint8_t>((value >> i) & 1u);
            emit(bit);
            if (bit == 0u) {
                ones_ = 0u;
            } else if (++ones_ == kOnesBeforeInsertion) {
                emit(uint8_t{0u});
                ones_ = 0u;
            }
        }
    }

    void     Reset() { ones_ = 0u; }
    uint8_t& Ones() { return ones_; }
    uint8_t  Ones() const { return ones_; }

    bool operator==(const Rcvdl56dpflZeroInsertion&) const = default;

private:
    static constexpr uint8_t kOnesBeforeInsertion = 5u;

    uint8_t ones_ = 0u;
};

class Rcvdl56dpflHdlcDeframer {
public:
    enum class Event : uint8_t { kNone, kByte, kFlag, kAbort };

    Event Bit(uint8_t bit, uint8_t* byte);
    void  Reset() { *this = Rcvdl56dpflHdlcDeframer{}; }

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

    bool operator==(const Rcvdl56dpflHdlcDeframer&) const = default;

private:
    Event DataBit(uint8_t bit, uint8_t* byte);

    uint8_t ones_   = 0u;
    uint8_t synced_ = 0u;
    uint8_t value_  = 0u;
    uint8_t count_  = 0u;
};

}
