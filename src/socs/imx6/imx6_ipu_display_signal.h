#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

class Imx6IpuDisplaySignal : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void Refresh();
    bool PanelEnabled() const;
    uint32_t ActiveDisplayChannel() const;
    void Reset();
    void ResetActiveDisplayChannel();
    void SaveState(StateWriter& writer);
    void RestoreState(StateReader& reader);

private:
    void ValidateTemplates();
    void ValidateMapping(uint32_t mapping) const;

    bool panel_enabled_ = false;
    uint32_t active_display_channel_ = 0xFFFFFFFFu;
};

}
