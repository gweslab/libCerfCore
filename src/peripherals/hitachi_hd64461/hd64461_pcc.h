#pragma once

#include <cstdint>
#include <mutex>

#include "../../socs/reg_window_impl.h"
#include "../pcmcia/pcmcia_slot.h"

namespace cerfcore {

class Hd64461Gpio;
class Hd64461Intc;
class Hd64461SocketWiring;
class Hd64461SystemControl;

namespace cerf_hd64461_pcc_detail {

using cerf_reg_window_detail::OtherReset;
using cerf_reg_window_detail::ReadKind;
using cerf_reg_window_detail::RegSpec;
using cerf_reg_window_detail::RegWindowModel;
using cerf_reg_window_detail::WriteKind;

constexpr RegSpec InterfaceStatus() {
    return RegSpec{ReadKind::kSampled, WriteKind::kFatal, 0x00u, 0x00u, 0x00u, OtherReset::kRetain,
                   0x00u, 0x00u, true};
}

constexpr RegSpec Control(uint16_t writable, uint16_t fatal_on_set) {
    return RegSpec{ReadKind::kStored, WriteKind::kStored, writable, 0x00u, fatal_on_set, OtherReset::kRetain,
                   0x00u, 0x00u, true};
}

constexpr RegSpec ChangeStatus(uint16_t writable, uint16_t reserved) {
    return RegSpec{ReadKind::kSampled, WriteKind::kHooked, writable, 0x00u, reserved, OtherReset::kRetain,
                   0x00u, 0x00u, true};
}

constexpr RegWindowModel Window() {
    RegWindowModel w{0x10002000u, 0x30u, 24u, false, {}};
    w.reg[0]  = InterfaceStatus();
    w.reg[1]  = Control(0xFFu, 0x00u);
    w.reg[2]  = ChangeStatus(0xBFu, 0x40u);
    w.reg[3]  = Control(0xFFu, 0x80u);
    w.reg[4]  = Control(0x03u, 0xFCu);
    w.reg[8]  = InterfaceStatus();
    w.reg[9]  = Control(0xDFu, 0x20u);
    w.reg[10] = ChangeStatus(0x8Fu, 0xF0u);
    w.reg[11] = Control(0x8Fu, 0xF0u);
    w.reg[12] = Control(0x03u, 0xFCu);
    w.reg[21] = Control(0x90u, 0x00u);
    w.reg[22] = Control(0x0Fu, 0xFFu);
    w.reg[23] = Control(0x03u, 0xFFu);
    return w;
}

inline constexpr RegWindowModel kModel = Window();

}

class Hd64461Pcc
    : public cerf_reg_window_detail::RegWindow<cerf_hd64461_pcc_detail::kModel, Peripheral, std::mutex>,
      public PcmciaSlotHost {
public:
    struct CardCycle {
        PcmciaSlot* slot;
        uint32_t    card_address;
        bool        attribute;
        bool        dropped;
    };

    struct SocketPins {
        bool ready;
        bool write_protect;
        bool vs2;
        bool vs1;
        bool cd2;
        bool cd1;
        bool bvd2;
        bool bvd1;
        bool wait;
    };

    explicit Hd64461Pcc(CerfEmulator& emu);

    bool ShouldRegister() override;
    void OnReady() override;
    void OnShutdown() override;

    void WriteByte(uint32_t addr, uint8_t value) override;
    void WriteHalf(uint32_t addr, uint16_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

    CardCycle  DecodeCycle(uint32_t area, uint32_t offset, bool io, bool write);
    SocketPins Pins(uint32_t ch);

    void    OnCardDetectChanged(PcmciaSlot& slot) override;
    void    OnReadyIreqLow(PcmciaSlot& slot) override;
    void    OnReadyIreqHigh(PcmciaSlot& slot) override;
    void    OnCardPowerChanged(PcmciaSlot& slot) override;
    uint8_t UndrivenDataLane() const override { return kUnstatedUndrivenDataLane; }

protected:
    uint16_t SampleRegister(uint32_t i, uint32_t addr) override;
    void     WriteHooked(uint32_t i, uint32_t addr, uint16_t value) override;
    void     AfterStore(uint32_t i) override;

private:
    struct SocketDrive {
        bool powered;
        bool reset;
        bool enabled;
    };

    uint32_t    ChannelOf(const PcmciaSlot& slot) const;
    bool        ReadyPinHighLocked(uint32_t ch) const;
    bool        IoInterfaceLocked(uint32_t ch) const;
    bool        IreqEdgeModeLocked() const;
    uint32_t    IreqModeLocked() const;
    SocketPins  PinsLocked(uint32_t ch) const;
    uint16_t    InterfaceStatusLocked(uint32_t ch) const;
    uint16_t    ChangeStatusLocked(uint32_t ch) const;
    bool        RequestLocked(uint32_t ch) const;
    void        LatchLocked(uint32_t ch, uint16_t bits);
    void        TrackReadyPinLocked(uint32_t ch);
    void        UpdateRequestLocked(uint32_t ch);
    SocketDrive SocketDriveLocked(uint32_t ch) const;
    void        ApplySocketDrive();
    void        SetReadyIreqLow(PcmciaSlot& slot, bool low);

    PcmciaSlot                  slot0_;
    PcmciaSlot                  slot1_;
    PcmciaSlot*                 slots_[2];
    Hd64461Intc*                intc_              = nullptr;
    const Hd64461Gpio*          gpio_              = nullptr;
    const Hd64461SocketWiring*  wiring_            = nullptr;
    const Hd64461SystemControl* system_            = nullptr;
    bool                        ready_ireq_low_[2] = {};
    bool                        ready_pin_high_[2] = {true, true};
};

}
