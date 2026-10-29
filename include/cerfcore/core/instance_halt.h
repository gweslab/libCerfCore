#pragma once

#include "log.h"

#include <cstdint>
#include <string>

namespace cerfcore {

class InstanceHalt {
public:
    int         Code() const { return code_; }
    std::string Report(const Log::CautionRing& cautions) const;

private:
    friend class Fatal;

    explicit InstanceHalt(int code);

    static constexpr int kMaxFrames = 32;

    int      code_;
    uint32_t thread_id_;
    int      frame_count_ = 0;
    void*    frames_[kMaxFrames];
};

}
