#pragma once

#include <cstdint>

namespace cerfcore {

class Rcvdl56dpflDataPumpHost {
public:
    virtual void     SetIrqLevel(bool asserted)           = 0;
    virtual void     SetBitRate(uint32_t bits_per_second) = 0;
    virtual uint64_t BitTickNow()                         = 0;
    virtual uint64_t BitTicksElapsed()                    = 0;
    virtual void     ArmTimer(uint64_t tick)              = 0;
    virtual int64_t  GuestNs()                            = 0;
    [[noreturn]] virtual void Halt(const char* reason)   = 0;

protected:
    ~Rcvdl56dpflDataPumpHost() = default;
};

}
