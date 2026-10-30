#pragma once

#include <cerfcore/core/service.h>

#include <array>
#include <cstdint>
#include <mutex>

namespace cerfcore {

class StateReader;
class StateWriter;

class CompaqIpaqH3000MicroKeys : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    void SetPressed(uint8_t key, bool pressed);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    static constexpr size_t kKeyCount = 128;

    void SendLocked(uint8_t key, bool pressed);

    std::mutex                       mtx_;
    std::array<uint8_t, kKeyCount>   held_{};
};

}
