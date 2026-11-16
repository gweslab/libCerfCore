#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class Imx6IpuDpSrmState : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void UpdateFromSrm();
    void Reset();
    uint32_t Register(uint32_t offset) const;
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);

private:
    std::array<uint32_t, 3> registers_{};
};

}
