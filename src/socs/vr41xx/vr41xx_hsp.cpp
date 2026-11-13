#include "vr41xx_hsp.h"

#include <utility>

namespace cerfcore {

void Vr41xxHsp::RegisterAferstListener(std::function<void(bool)> fn) {
    fn(aferst_high_);
    aferst_listeners_.Add(std::move(fn));
}

void Vr41xxHsp::DriveAferst(bool high) {
    aferst_high_ = high;
    aferst_listeners_.Call(high);
}

}
