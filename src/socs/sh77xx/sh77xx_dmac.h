#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include "sh77xx_standby_control.h"

#include <cstdint>

namespace cerfengine {
class ShBusMasterCoherence;
}

namespace cerfcore {

class PhysicalBus;
class Sh77xxIntc;

enum class Sh77xxDmaRequest : uint8_t {
    kIrdaTx,
    kIrdaRx,
    kScifTx,
    kScifRx,
    kAd,
    kCmt,
    kCount,
};

class Sh77xxDmac : public Peripheral {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    uint32_t MmioBase() const override { return 0x04000020u; }
    uint32_t MmioSize() const override { return 0x42u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint32_t ReadWord(uint32_t addr) override;
    void     WriteWord(uint32_t addr, uint32_t value) override;

    void Request(Sh77xxDmaRequest source);
    void Withdraw(Sh77xxDmaRequest source);
    bool TakesRequest(Sh77xxDmaRequest source) const;
    bool Transferring(Sh77xxDmaRequest source) const;
    bool ClockStopped() const;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

private:
    static constexpr uint32_t kChannels = 4u;

    struct Channel {
        uint32_t sar          = 0u;
        uint32_t dar          = 0u;
        uint32_t tcr          = 0u;
        uint32_t chcr         = 0u;
        uint32_t reload_base  = 0u;
        uint32_t reload_count = 0u;
        uint8_t  stale        = 0u;
        bool     tcr_armed    = true;
        bool     te_read      = false;
    };

    enum class Field : uint8_t { kSar, kDar, kTcr, kChcr };

    Channel& Locate(uint32_t addr, Field& field);
    uint32_t ReadChannel(Channel& ch, Field field);
    void     WriteChannel(Channel& ch, Field field, uint32_t value);
    void     WriteChcr(Channel& ch, uint32_t value);
    void     WriteDmaor(uint16_t value);
    uint32_t Index(const Channel& ch) const;
    bool     Armed(const Channel& ch) const;
    bool     Enabled(const Channel& ch) const;
    bool     Requested(const Channel& ch) const;
    void     ValidateFields(const Channel& ch);
    void     ValidateStart(const Channel& ch);
    void     ValidateRequestRegister(const Channel& ch, uint32_t rs);
    void     AbortReload(Channel& ch);
    void     ClearModuleRequest(uint32_t rs);
    int      Pick();
    void     Service();
    void     TransferUnit(Channel& ch);
    uint32_t BusRead(const Channel& ch, uint32_t addr, uint32_t bytes);
    void     BusWrite(const Channel& ch, uint32_t addr, uint32_t bytes, uint32_t value);
    uint32_t BusAddress(const Channel& ch, uint32_t addr, uint32_t bytes, const char* side);
    void     Drive(const Channel& ch);
    void     Initialize();

    Sh77xxIntc*   intc_   = nullptr;
    Sh77xxStbcr2* stbcr2_ = nullptr;
    PhysicalBus*  bus_    = nullptr;
    cerfengine::ShBusMasterCoherence* coherence_ = nullptr;
    Channel       ch_[kChannels];
    uint16_t      dmaor_           = 0u;
    uint8_t       pending_         = 0u;
    uint8_t       rr_first_        = 0u;
    uint8_t       transferring_rs_ = 0u;
    bool          servicing_       = false;
};

}
