#pragma once

#include <cerfcore/peripherals/peripheral_base.h>
#include <cerfcore/core/callback_list.h>

#include <atomic>
#include <cstdint>
#include <functional>

namespace cerfcore {

class IbmWorkpadZ50Battery;
class IbmWorkpadZ50MemberStraps;

class IbmWorkpadZ50GpioBuffers : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return kCs0Base; }
    uint32_t MmioSize() const override { return kCs1Offset + 1u; }

    uint8_t ReadByte (uint32_t addr) override;
    void    WriteByte(uint32_t addr, uint8_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    bool MicEnabled() const;
    void RegisterOutputListener(std::function<void()> fn);

private:
    static constexpr uint32_t kCs0Base   = 0x16100000u;
    static constexpr uint32_t kCs1Offset = 0x00040000u;

    uint8_t Cs0Inputs() const;
    uint8_t Cs1Inputs() const;
    void    NotifyOutputListeners();

    const IbmWorkpadZ50MemberStraps* straps_  = nullptr;
    const IbmWorkpadZ50Battery*      battery_ = nullptr;
    uint8_t                          extension_ram_strap_ = 0;
    std::atomic<uint8_t> cs0_out_{0};
    std::atomic<uint8_t> cs1_out_{0};
    CallbackList<> output_listeners_;
};

}
