#pragma once

#include "../../socs/stm32f2/stm32f2_gpio.h"
#include "../../socs/stm32f2/stm32f2_mcu.h"

#include <array>
#include <cstdint>
#include <vector>

namespace cerfcore {

class StateReader;
class StateWriter;

namespace siemens_simatic_mobile_panels_gen2 {

class FModuleBootloader {
public:
    static constexpr uint32_t kBootloaderStubOffset = 0x100u; /* Stand-in stub, real bootloader not available. */
    static constexpr uint32_t kRegionBytes = 0xC000u;

    explicit FModuleBootloader(stm32f2::Mcu& mcu) : mcu_(mcu) {}

    void Connect(stm32f2::Gpio* partner_port_b) { partner_port_b_ = partner_port_b; }

    static void Install(std::vector<uint8_t>& flash);

    bool Active() const { return active_; }
    void Enter(int64_t now_ns);
    void Run(int64_t now_ns);

    bool Updated() const { return updated_; }
    void StartApplication();
    bool Erased() const { return erased_; }

    void SaveState(StateWriter& writer) const;
    void RestoreState(StateReader& reader);

private:
    enum Step : uint32_t { kResync, kTransfer };

    void Drive(uint32_t state);
    void ServeUsart();
    void Mirror(uint32_t state);
    bool TransferReady(uint32_t state) const;
    void TransferStarted();
    void TransferFinished();
    void Accept(uint8_t tag, const uint8_t* data);
    void QueueReply(uint8_t tag, const uint8_t* data, uint32_t bytes);
    bool FlashStep();
    void UnlockFlash();
    void LockFlash();
    void CheckFlash(const char* what);

    static constexpr uint32_t kMaxFrameBytes = 140u;
    static constexpr uint32_t kMaxReplyBytes = 8u;

    stm32f2::Mcu& mcu_;
    stm32f2::Gpio* partner_port_b_ = nullptr;

    bool active_ = false;
    uint32_t step_ = kResync;
    bool resync_next_ = false;
    uint32_t state_ = 0u;
    bool own_failed_ = false;
    std::array<uint8_t, kMaxFrameBytes> rx_{};
    uint32_t rx_bytes_ = 0u;
    std::array<uint8_t, kMaxReplyBytes> reply_{};
    uint32_t reply_bytes_ = 0u;
    uint32_t tx_sent_ = 0u;
    bool sending_ = false;
    uint8_t sequence_ = 0u;
    bool end_record_ = false;
    int64_t reply_after_ns_ = 0;

    bool application_target_ = false;
    bool erase_queued_ = false;
    uint32_t erase_next_ = 0u;
    std::array<uint8_t, 128> program_data_{};
    uint32_t program_offset_ = 0u;
    uint32_t program_left_ = 0u;
    bool flash_op_ = false;
    bool erased_ = false;
    uint32_t records_ = 0u;
    uint32_t programmed_ = 0u;
    bool updated_ = false;
};

}

}
