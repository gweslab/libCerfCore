#pragma once

#include "../../execution/guest_cycle_clock.h"
#include "../../socs/i2c_slave.h"
#include "atmel_mxt224e_map.h"
#include <cerfcore/core/callback_list.h>

#include <array>
#include <cstdint>

namespace cerfcore {

class AtmelMxt224e : public I2cSlave {
public:
    using I2cSlave::I2cSlave;
    using Message = std::array<uint8_t, kMxtMessageBytes>;

    void OnReady() override;

    bool    MatchesAddress(uint8_t slave_addr) const override;
    bool    OwnsAddress(uint8_t slave_addr) const override;
    void    TxnStart(uint8_t slave_addr) override;
    bool    TxnWriteByte(uint8_t slave_addr, uint8_t byte) override;
    uint8_t TxnReadByte(uint8_t slave_addr) override;
    void    BusStart() override;
    void    BusStop() override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    void AddChgListener(CallbackList<bool>::Callback listener);
    void PowerOn();
    void SetResetLine(bool asserted);

    uint8_t  Config(uint16_t addr) const { return ram_[addr - kMxtNvmStart]; }
    uint16_t ConfigRange(uint16_t addr) const;
    bool     TouchscreenScanning() const;
    void     PostTouch(uint8_t touch, const Message& data);

    virtual bool XLinesFollowSurfaceY() const = 0;

protected:
    virtual uint8_t DeviceAddress() const = 0;

private:
    static constexpr uint16_t kT9TrackedBytes = 5u;

    void     BuildInfoBlock();
    uint8_t  ReadByteAt(uint16_t addr);
    uint8_t  ReadMessageByte(uint16_t offset);
    void     WriteByteAt(uint16_t addr, uint8_t value);
    void     FinishWrite();
    bool     RunCommands();
    bool     TouchObjectEnabled() const;
    void     UpdateConfigError();
    void     ReportStatus();
    void     PostStatus();
    void     Post(uint8_t report_id, const Message& data, bool merge_events);
    void     PopHead();
    void     DriveChg();
    void     ArmConfigErrorRepeat();

    std::array<uint8_t, kMxtInfoBlockEnd>    info_{};
    std::array<uint8_t, kMxtNvmBytes>        ram_{};
    std::array<uint8_t, kMxtT6Bytes>         t6_{};
    std::array<uint8_t, kT9TrackedBytes>     t9_before_{};
    std::array<Message, kMxtReportIdCount>   pending_{};
    static_assert(sizeof(std::array<Message, kMxtReportIdCount>) == kMxtReportIdCount * kMxtMessageBytes);
    std::array<uint8_t, kMxtReportIdCount>   order_{};
    Message                                  current_{};
    CallbackList<bool>                       chg_listeners_;
    GuestCycleClock::Event*                  cfgerr_event_    = nullptr;
    int64_t                                  cfgerr_due_ns_   = 0;
    uint16_t                                 pointer_         = 0u;
    uint16_t                                 cursor_          = 0u;
    uint8_t                                  address_low_     = 0u;
    uint8_t                                  address_bytes_   = 0u;
    uint8_t                                  queued_          = 0u;
    uint8_t                                  status_one_off_  = 0u;
    uint8_t                                  active_mode_     = 0u;
    bool                                     wrote_           = false;
    bool                                     t6_written_      = false;
    bool                                     head_offered_    = false;
    bool                                     chg_held_high_   = false;
    bool                                     cfgerr_          = false;
    bool                                     cfgerr_armed_    = false;
    bool                                     in_reset_        = false;
    bool                                     status_pending_  = false;
};

}
