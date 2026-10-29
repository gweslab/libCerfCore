#pragma once

#include "fatal.h"
#include "service.h"

#include <thread>
#include <utility>

namespace cerfcore {

class InstanceThreads : public Service {
public:
    using Service::Service;

    void OnReady() override;

    template <class Fn>
    std::thread Start(Fn&& fn);

    template <class Fn, class After>
    std::thread Start(Fn&& fn, After&& after);

private:
    Fatal* fatal_ = nullptr;
};

template <class Fn>
std::thread InstanceThreads::Start(Fn&& fn) {
    return std::thread([fatal = fatal_, body = std::forward<Fn>(fn)]() mutable { fatal->Guard(body); });
}

template <class Fn, class After>
std::thread InstanceThreads::Start(Fn&& fn, After&& after) {
    return std::thread(
        [fatal = fatal_, body = std::forward<Fn>(fn), then = std::forward<After>(after)]() mutable {
            fatal->Guard(body);
            fatal->GuardTeardown(then);
        });
}

}
