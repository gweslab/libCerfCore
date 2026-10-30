#pragma once

#include <cerfcore/core/service.h>
#include "../../host/battery_widget.h"

#include <cstdint>

namespace cerfcore {

class CompaqIpaqH3000Battery : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    static constexpr uint8_t kReplyLength = 5;

    void Reply(uint8_t* out) const;

    bool OnAc() const;
    bool Charging() const;

private:
    static bool IsCharging(bool on_ac, int fill_percent);
    bool        PcCardPowered() const;
    uint16_t    LoadSag() const;

    BatteryWidget battery_;
};

}
