#pragma once

#include <cerfcore/core/service.h>

#include "../../platform/wave_in_fifo.h"

#include <cstdint>

namespace cerfcore {

class AudioActivityWidget;

class Hp600Sh7709Microphone : public Service {
public:
    using Service::Service;

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void     SetRate(uint32_t rate_hz);
    uint16_t Convert();

private:
    WaveInFifo           mic_;
    AudioActivityWidget* activity_ = nullptr;
};

}
