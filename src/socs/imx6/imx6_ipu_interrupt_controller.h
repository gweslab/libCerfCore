#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class Imx6IpuInterruptController : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    uint32_t ReadRegister(uint32_t offset) const;
    void ClearStatus(uint32_t offset, uint32_t mask);
    void WriteControl(uint32_t offset, uint32_t value);
    void Raise(uint32_t irq);
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);
    void PostReset();
    void PostRestore();

private:
    void UpdateLines();

    std::array<uint32_t, 15> status_{};
    std::array<uint32_t, 15> control_{};
    bool sync_irq_asserted_ = false;
    bool err_irq_asserted_ = false;
};

}
