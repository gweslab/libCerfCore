#pragma once

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

enum class UlpiVbus : uint8_t {
    kBelowSessionEnd,
    kSessionEnd,
    kSessionValid,
    kVbusValid,
};

class UlpiPhy {
public:
    static constexpr uint8_t kFunctionControl = 0x04u;
    static constexpr uint8_t kOtgControl      = 0x0Au;
    static constexpr uint8_t kDebug           = 0x15u;

    static constexpr uint8_t kStatusVbusValid = 1u << 1;
    static constexpr uint8_t kStatusSessValid = 1u << 2;
    static constexpr uint8_t kStatusSessEnd   = 1u << 3;
    static constexpr uint8_t kStatusIdGnd     = 1u << 4;

    virtual ~UlpiPhy() = default;

    static constexpr uint8_t kLineStateMask = 0x03u;

    bool Read(uint8_t addr, uint8_t& value);
    bool Write(uint8_t addr, uint8_t value);

    bool Synchronous() const;
    bool ClockRuns() const;
    void Wakeup();

    bool    SetVbus(UlpiVbus level);
    uint8_t UtmiOutputs() const;

    virtual uint8_t LineState() const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

protected:
    static constexpr uint8_t kRegisterCount = 0x40u;

    void PowerOnStandard(const uint8_t (&id)[4]);
    bool ReadSet(uint8_t first, uint8_t& value) const;
    void WriteSet(uint8_t first, uint8_t addr, uint8_t value);

    virtual bool ReadVendor(uint8_t addr, uint8_t& value) const = 0;
    virtual bool WriteVendor(uint8_t addr, uint8_t value)       = 0;

    uint8_t  regs_[kRegisterCount] = {};
    UlpiVbus vbus_                 = UlpiVbus::kBelowSessionEnd;
};

}
