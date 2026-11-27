#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../../socs/i2c_slave.h"
#include "../../socs/oscillator_ticks.h"
#include <cerfcore/core/callback_list.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class AvagoQpdsT900 : public I2cSlave {
public:
    using I2cSlave::I2cSlave;

    bool ShouldRegister() override;
    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void AddIntListener(CallbackList<bool>::Callback listener);
    void PowerOn();

private:
    static constexpr uint32_t kControlRegisters = 16u;
    static constexpr uint32_t kDataWords        = 3u;

    enum class Phase : uint8_t { kSleep, kStartup, kStart, kProx, kWait, kAlsDelay, kAls };

    struct Machine {
        Phase                            phase     = Phase::kSleep;
        uint64_t                         phase_end = 0u;
        uint8_t                          valid     = 0u;
        uint8_t                          prox_out  = 0u;
        uint8_t                          als_out   = 0u;
        bool                             pint      = false;
        bool                             aint      = false;
        std::array<uint16_t, kDataWords> data{};
    };

    static bool Timed(Phase phase);

    void     WriteCommand(uint8_t byte);
    void     SpecialFunction(uint8_t function);
    void     WriteRegister(uint8_t reg, uint8_t value);
    uint8_t  ReadRegister(uint8_t reg);
    uint8_t  ReadData(uint8_t reg);
    void     NextAddress();
    uint16_t RegWord(uint8_t low) const;
    uint64_t ProxSpan() const;
    uint64_t WaitSpan() const;
    uint64_t AlsSpan() const;
    uint64_t CycleSpan() const;
    uint32_t ProxPersistence() const;
    uint32_t AlsPersistence() const;
    void     Run(Machine& m, uint64_t now) const;
    void     CycleStart(Machine& m, uint64_t at, uint64_t now) const;
    void     WaitCheck(Machine& m, uint64_t at, uint64_t now) const;
    void     AlsCheck(Machine& m, uint64_t at, uint64_t now) const;
    void     ProxDone(Machine& m, uint64_t count) const;
    void     AlsDone(Machine& m, uint64_t count) const;
    uint64_t NextRaise(uint64_t now) const;
    void     Advance();
    void     Schedule();
    void     DriveInt();

    GuestCycleClock*                       clock_ = nullptr;
    GuestCycleClock::Event*                event_ = nullptr;
    OscillatorTicks                        ticks_{emu_, false};
    CallbackList<bool>                     int_listeners_;
    std::array<uint8_t, kControlRegisters> regs_{};
    std::array<uint8_t, kDataWords>        shadow_{};
    uint8_t                                address_         = 0u;
    uint8_t                                type_            = 0u;
    bool                                   command_pending_ = false;
    Machine                                m_{};
};

}
