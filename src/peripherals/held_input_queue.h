#pragma once

#include <cstddef>
#include <deque>

namespace cerfcore {

template <typename State>
class HeldInputQueue {
public:
    bool         Empty() const { return states_.empty(); }
    State&       Back() { return states_.back(); }
    const State& Back() const { return states_.back(); }
    void         Clear() { states_.clear(); }

    void Push(const State& state) {
        if (states_.size() == kLimit) states_.pop_front();
        states_.push_back(state);
    }

    State Pop() {
        State state = states_.front();
        states_.pop_front();
        return state;
    }

private:
    static constexpr size_t kLimit = 256;

    std::deque<State> states_;
};

}
