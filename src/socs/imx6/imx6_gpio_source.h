#pragma once

#include <cstdint>
#include <functional>

#include <cerfcore/core/service.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

class Imx6GpioInputSource : public Service {
public:
    using Service::Service;

    virtual uint32_t GpioBase() const = 0;

    virtual uint32_t ApplyPadInputs(uint32_t inputs) { return inputs; }

    virtual uint32_t DrivenPins() const { return 0u; }
    virtual uint32_t PendingIsr() { return 0; }
    virtual void OnIsrClear(uint32_t) {}
    virtual uint32_t ApplyDataRead(uint32_t dr) { return dr; }
    virtual void OnEffectiveOutputs(uint32_t, uint32_t) {}

    virtual void SaveState(StateWriter&) {}
    virtual void RestoreState(StateReader&) {}
    virtual void PostRestore() { Reevaluate(); }
    virtual void OnControllerReset() {}

    void SetReevaluate(std::function<void()> fn) { reevaluate_ = std::move(fn); }

protected:
    void Reevaluate() {
        if (reevaluate_) reevaluate_();
    }

private:
    std::function<void()> reevaluate_;
};

}
