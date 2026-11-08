#pragma once

#include "../core/service.h"

#include <shared_mutex>

namespace cerfcore {

class EmulationFreeze : public Service {
public:
    using Service::Service;

    [[nodiscard]] std::shared_lock<std::shared_mutex> WorkerSection() {
        return std::shared_lock<std::shared_mutex>(mtx_);
    }

    [[nodiscard]] std::shared_lock<std::shared_mutex> TryWorkerSection() {
        return std::shared_lock<std::shared_mutex>(mtx_, std::try_to_lock);
    }

    [[nodiscard]] std::unique_lock<std::shared_mutex> SnapshotSection() {
        return std::unique_lock<std::shared_mutex>(mtx_);
    }

private:
    std::shared_mutex mtx_;
};

}
