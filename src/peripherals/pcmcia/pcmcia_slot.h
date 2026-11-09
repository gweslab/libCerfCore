#pragma once

#include "pcmcia_card.h"
#include "../external_device_clock.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

namespace cerfcore {

class CerfEmulator;
class PcmciaSlot;

class PcmciaSlotHost {
public:
    virtual ~PcmciaSlotHost() = default;
    virtual void OnCardDetectChanged(PcmciaSlot& slot) = 0;
    virtual void OnReadyIreqLow     (PcmciaSlot& slot) = 0;
    virtual void OnReadyIreqHigh    (PcmciaSlot& slot) = 0;
    virtual void OnCardPowerChanged (PcmciaSlot&) {}
    virtual uint8_t UndrivenDataLane() const = 0;

    static constexpr uint8_t kUnstatedUndrivenDataLane = 0xFFu;
};

/* One physical PCMCIA socket. Lock hierarchy: bus_mutex_ > backend rx-callback
   mutex > card mutex > controller mutex. */
class PcmciaSlot : public HostWidget {
public:
    PcmciaSlot(CerfEmulator& emu, PcmciaSlotHost& host, std::string label);

    void SetLabel(std::string label) { label_ = std::move(label); }

    bool HasCard()   const { return (pins_.load(std::memory_order_acquire) & kPinPresent) != 0u; }
    bool IsPowered() const {
        constexpr uint8_t live = kPinPresent | kPinPowered;
        return (pins_.load(std::memory_order_acquire) & live) == live;
    }
    bool Vs1Grounded()   const { return (pins_.load(std::memory_order_acquire) & kPinVs1Grounded) != 0u; }
    bool Vs2Grounded()   const { return (pins_.load(std::memory_order_acquire) & kPinVs2Grounded) != 0u; }
    bool LowVoltageKey() const { return (pins_.load(std::memory_order_acquire) & kPinLowVoltageKey) != 0u; }
    void SetPowered(bool on);
    void PowerCycleCard();
    void SetCardEnabled(bool on);
    void SetResetLine(bool asserted);

    uint8_t  ReadAttribute8 (uint32_t offset);
    void     WriteAttribute8(uint32_t offset, uint8_t value);
    uint16_t ReadAttribute16 (uint32_t offset);
    void     WriteAttribute16(uint32_t offset, uint16_t value);
    uint8_t  ReadCommon8  (uint32_t offset);
    uint16_t ReadCommon16 (uint32_t offset);
    void     WriteCommon8 (uint32_t offset, uint8_t  value);
    void     WriteCommon16(uint32_t offset, uint16_t value);
    uint8_t  ReadIo8  (uint32_t offset);
    uint16_t ReadIo16 (uint32_t offset);
    void     WriteIo8 (uint32_t offset, uint8_t  value);
    void     WriteIo16(uint32_t offset, uint16_t value);
    bool     Iois16   (uint32_t offset);
    bool     InputAcknowledge(uint32_t offset);
    uint8_t  UndrivenDataLane() const { return host_.UndrivenDataLane(); }
    uint16_t UndrivenDataWord() const;

    /* Card-side IRQ line, callable from any card thread; no bus lock. */
    void RaiseIrq();
    void ClearIrq();

    void ArmCardTimer(int64_t delay_ns);
    void DisarmCardTimer();

    void InsertCard(std::unique_ptr<PcmciaCard> card);

    /* Eject on behalf of a card whose host resource died, keyed on the residency id
       it was given at insert: the request is dispatched from a UI job, by which time
       the card may already be gone and another may hold the slot. */
    void EjectCardIfResident(uint64_t card_id);

    /* Forward the CerfEmulator quiesce phase to the resident card so it can
       detach from peers before any service is destroyed. Owning controllers
       call this from their Service::OnShutdown. */
    void OnShutdown();

    void SaveSlotState(StateWriter& w);
    void RestoreSlotState(StateReader& r);
    void PostRestoreSlot();

    /* HostWidget. */
    std::string WidgetName() const override { return label_; }
    WidgetGroup Group() const override { return WidgetGroup::Pcmcia; }
    std::string Tooltip() const override;
    std::vector<WidgetControl> Controls() override;
    WidgetIcon  Icon() const override;
    bool PollDirty() override;

private:
    static constexpr uint8_t kPinPresent = 0x01u;
    static constexpr uint8_t kPinPowered = 0x02u;
    static constexpr uint8_t kPinVs1Grounded   = 0x04u;
    static constexpr uint8_t kPinVs2Grounded   = 0x08u;
    static constexpr uint8_t kPinLowVoltageKey = 0x10u;
    static constexpr uint8_t kLineIreq = 0x01u;
    static constexpr uint8_t kLineBusy = 0x02u;
    struct UiView {
        const char*                icon = "pcmcia";
        std::string                tooltip_detail;
        std::string                save_id;
        std::string                display_name;
        std::vector<WidgetControl> card_controls;
        uint64_t                   generation = 0;
        bool                       has_card   = false;
    };

    void PublishPinsLocked();
    void PublishUiLocked();
    void DriveLine(uint8_t source, bool low);
    bool CardSelectedLocked();

    void InsertLocked(std::unique_ptr<PcmciaCard> card);
    void EjectLocked();
    void OnCardTimer();
    ExternalDeviceClock::Timer& CardTimer();

    /* Menu actions; gen guards against stale popup callbacks firing
       after the slot's contents changed. */
    void MenuEject(uint64_t gen);
    void CombinedSwap(uint64_t gen, std::unique_ptr<PcmciaCard> card);
    void MenuInsert(uint64_t gen, const std::string& card_id);
    /* Insert a pre-built card (gen-guarded) - used by card kinds whose
       insert menu resolves a host resource (e.g. CompactFlash image). */
    void MenuInsertCard(uint64_t gen, std::unique_ptr<PcmciaCard> card);
    std::vector<SlotEntry> Available(uint64_t gen);
    WidgetControl GuardControlLocked(WidgetControl control, uint64_t gen);
    template <class Fn>
    Fn GuardLocked(Fn fn, uint64_t gen);

    CerfEmulator&   emu_;
    PcmciaSlotHost& host_;
    std::string     label_;

    mutable std::mutex          bus_mutex_;
    std::unique_ptr<PcmciaCard> card_;
    bool                        powered_ = false;
    bool                        enabled_ = true;
    bool                        reset_asserted_ = false;
    uint64_t                    generation_ = 0;
    std::atomic<uint8_t>        pins_{0};
    std::atomic<uint8_t>        line_{0};

    ExternalDeviceClock::Timer* card_timer_ = nullptr;

    mutable std::mutex ui_mtx_;
    UiView             ui_;
    std::string        ui_last_res_;
};

}
