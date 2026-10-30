#pragma once

#include <cerfcore/core/service.h>
#include <cerfcore/host/host_widget.h>

#include <cstdint>
#include <mutex>

namespace cerfcore {

class CompaqIpaqH3000Battery;
class StateReader;
class StateWriter;

class CompaqIpaqH3000MicroLed : public Service, public HostWidget {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;

    void SetLed(const uint8_t* req, uint8_t len);

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    std::string WidgetName() const override { return "Alarm/charge LED"; }
    WidgetGroup Group() const override { return WidgetGroup::Indicator; }
    std::string Tooltip() const override;
    WidgetIcon  Icon() const override;
    bool        PollDirty() override;

private:
    enum class Glow : uint8_t { kDark, kGreen, kYellow };

    struct Setting {
        uint8_t color    = 0;
        uint8_t flags    = 0;
        uint8_t on_time  = 0;
        uint8_t off_time = 0;
    };

    Setting Current() const;
    Glow    GlowNow() const;

    mutable std::mutex      mtx_;
    Setting                 led_{};
    CompaqIpaqH3000Battery* battery_ = nullptr;

    Glow last_drawn_ = Glow::kDark;
    bool drawn_once_ = false;
};

}
