#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace cerfcore {

class PppLink {
public:
    virtual ~PppLink() = default;

    virtual void PushRx(const uint8_t* data, size_t n) = 0;
    virtual bool RxEmpty() const = 0;

    using RxDrainFn = std::function<void()>;
    virtual void SetRxDrainCallback(RxDrainFn cb) = 0;
};

}
