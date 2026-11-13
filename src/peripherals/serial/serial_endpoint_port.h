#pragma once

#include "serial_line.h"
#include "serial_line_timer.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace cerfcore {

class CerfEmulator;
class HostWidget;
class SerialCradle;
class SerialEndpoint;
class StateWriter;
class StateReader;

struct SerialControlLines {
    bool dtr = false;
    bool rts = false;
};

class SerialEndpointPort {
public:
    using ControlFn = std::function<SerialControlLines()>;

    SerialEndpointPort(SerialLine& line, SerialLineTimer& timer, ControlFn control);
    ~SerialEndpointPort();

    void AttachCradle(CerfEmulator& emu, std::string label);
    void SetActivityWidget(HostWidget* widget) { activity_ = widget; }
    void OnShutdown();

    void SetEndpoint(SerialEndpoint* ep);
    void NotifyControlLines();
    void ResendModemInputs();
    bool DeliverTx(const uint8_t* data, size_t n, bool wire_connected);
    void MarkTx();
    void MarkRx();

    void SetRxDrainCallback(SerialLine::RxDrainFn cb);
    void NotifyRxDrained();
    void SetLineConfigCallback(SerialLine::LineConfigFn cb);
    void NotifyLineConfig(const SerialLine::LineConfig& cfg);

    void ArmTimer(int64_t delay_ns);
    void DisarmTimer();
    void OnTimer();

    void SaveCradleState(StateWriter& w);
    void RestoreCradleState(StateReader& r);
    void SaveEndpointState(StateWriter& w) const;
    void RestoreEndpointState(StateReader& r);
    void PostRestore();

private:
    SerialLine&      line_;
    SerialLineTimer& timer_;
    ControlFn        control_;
    HostWidget*      activity_ = nullptr;

    mutable std::recursive_mutex mu_;
    SerialEndpoint*              endpoint_ = nullptr;
    SerialLine::RxDrainFn        rx_drain_;
    SerialLine::LineConfigFn     line_cfg_cb_;
    bool                         lines_known_ = false;
    bool                         sent_dtr_    = false;
    bool                         sent_rts_    = false;

    std::unique_ptr<SerialCradle> cradle_;
};

}
