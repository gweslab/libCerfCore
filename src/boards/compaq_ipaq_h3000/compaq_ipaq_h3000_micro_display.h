#pragma once

#include <cerfcore/core/service.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

class StateReader;
class StateWriter;

class CompaqIpaqH3000MicroDisplay : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;

    uint8_t Backlight(const uint8_t* req, uint8_t len, uint8_t* reply);
    void    DisplayControl(const uint8_t* req, uint8_t len);

    uint32_t BacklightLoad() const;

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

private:
    mutable std::mutex mtx_;
    uint8_t backlight_mode_         = 0;
    uint8_t backlight_on_           = 0;
    uint8_t backlight_level_        = 0;
    uint8_t control_backlight_on_   = 0;
    uint8_t contrast_               = 0;
};

}
