#pragma once

#include "wave_out_backend.h"
#include "wave_out_callback_loop.h"

#include <condition_variable>
#include <cstdint>
#include <deque>

namespace cerfcore {

class WaveOutQueuedBackend : public WaveOutBackend {
public:
    void PostCommand(uint32_t command) final;
    void Play(uint32_t slot, const uint8_t* bytes, uint32_t length) final;

protected:
    struct Block {
        uint32_t       slot;
        const uint8_t* bytes;
        uint32_t       length;
    };

    void Begin(StartHandler on_start, CommandHandler on_command, DoneHandler on_done) final;
    virtual void StartWorker() {}
    void CompleteQueuedLocked();

    std::deque<Block>       writes_;
    std::condition_variable work_cv_;
    WaveOutCallbackLoop     callbacks_;
    bool                    shutdown_ = false;

private:
    void RetryOpen();
};

}
