#pragma once

#include "../../execution/guest_cycle_clock.h"
#include <cerfcore/peripherals/peripheral_base.h>
#include "vr41xx_giu.h"
#include "vr41xx_kiu_key_matrix.h"
#include "vr41xx_kiu_sequencer.h"
#include "vr41xx_rtcx_ticks.h"
#include <cerfcore/core/callback_list.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>

namespace cerfengine {
class HostRequestChannel;
}

namespace cerfcore {

class Vr41xxKiuKeyPins;

struct Vr41xxKiuModel {
    uint32_t stprep_zero_count;
    bool     scanstart_auto_clear;
    bool     keyen_scanline_interlock;
    bool     keyen_stop_deferred;
    bool     scanstart_while_running_defined;
    bool     gpen_retained_on_other_reset;
    bool     gpen_survives_kiurst;
    bool     scanline_none_drives_all;
    bool     scan_pins_drive_piod;
};

/* NEC VR41xx KIU (Keyboard Interface Unit): VR4102 UM ch.21, VR4111 UM ch.22, VR4121 UM
   ch.22. Register map: VR4102 Table 21-1, VR4111 Table 22-1 p459, VR4121 Table 22-1. */
class Vr41xxKiu : public Peripheral, private Vr41xxKiuSequencerHost {
public:
    using Peripheral::Peripheral;

    void OnReady() override;

    uint32_t MmioBase() const override { return 0x0B000180u; }
    uint32_t MmioSize() const override { return 0x20u; }

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;
    uint8_t  ReadByte (uint32_t addr) override { HaltUnsupportedAccess("KIU ReadByte", addr, 0); }
    uint32_t ReadWord (uint32_t addr) override { HaltUnsupportedAccess("KIU ReadWord", addr, 0); }
    void WriteByte(uint32_t addr, uint8_t  v) override { HaltUnsupportedAccess("KIU WriteByte", addr, v); }
    void WriteWord(uint32_t addr, uint32_t v) override { HaltUnsupportedAccess("KIU WriteWord", addr, v); }

    /* matrix_index = 16*KIUDATn + bit (0..95) is the KIUDAT0-5 layout storing KSCAN0..11 x
       KPORT[7..0] (VR4111 UM 22.2.1 p461, VR4102 UM 21.2.1 p424, VR4121 UM 22.2.1 p513).
       nec_mobilepro_700_ce2 keybddr.dll sub_15B4848 @0x15B4848 consumes that index into its
       96-entry table at 0x15B0978. */
    void SetKeyState(uint8_t matrix_index, bool pressed);

    void RegisterKportListener(std::function<void(uint8_t kport)> fn);

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;
    void PostRestore() override;

protected:
    virtual const Vr41xxKiuModel& Model() const = 0;

private:
    mutable std::mutex mtx_;

    Vr41xxKiuKeyMatrix key_matrix_;
    Vr41xxKiuSequencer seq_{*this};
    /* KIUINT, both reset rows 0 (VR4111 UM 22.2.6 p468, VR4102 UM 21.2.6 p431,
       VR4121 UM 22.2.6 p520). */
    uint16_t causes_    = 0;
    /* KIUWKI WINTVL(9:0) x 30 us, 0 = No Wait, reset rows 0 (VR4111 UM 22.2.5 p467,
       VR4102 UM 21.2.5 p430, VR4121 UM 22.2.5 p519). */
    uint16_t wintvl_    = 0;
    /* KIUWKS T3CNT/T2CNT/T1CNT; D14:0 reset to 1 (VR4111 UM 22.2.4 p466,
       VR4102 UM 21.2.4 p429, VR4121 UM 22.2.4 p518). */
    uint16_t wks_       = 0x7FFF;
    /* SCANLINE LINE[1:0], both reset rows 0 (VR4111 UM 22.2.9 p472, VR4102 UM 21.2.9 p434,
       VR4121 UM 22.2.9 p524). */
    uint16_t scanline_  = 0;
    uint16_t gpen_      = 0;
    /* KIUDAT0-5 hold data no read has taken since the last scan stored it (VR4111 UM 22.2.6
       p468 D2 KDATLOST, VR4102 UM 21.2.6 p431, VR4121 UM 22.2.6 p520). */
    bool     data_unread_ = false;
    bool     scan_in_flight_ = false;
    uint32_t next_slot_ = 0;
    uint16_t scan_wks_ = 0x7FFF;
    bool     scan_key_data_ = false;
    bool     undefined_ = false;
    bool     hibernating_ = false;
    uint8_t  kport_ = 0;
    CallbackList<uint8_t> kport_listeners_;
    /* End of the scan period or of the KIUWKI interval now in progress (VR4111 UM 22.2.4
       p466 / 22.2.5 p467, VR4102 UM 21.2.4 p429 / 21.2.5 p430, VR4121 UM 22.2.4 p518 /
       22.2.5 p519). */
    uint64_t phase_end_ = 0;

    GuestCycleClock*        clock_ = nullptr;
    GuestCycleClock::Event* event_ = nullptr;
    Vr41xxKiuKeyPins*       key_pins_ = nullptr;
    Vr41xxGiu*              giu_ = nullptr;
    cerfengine::HostRequestChannel* host_requests_ = nullptr;
    uint64_t                drive_signature_ = 0;
    std::atomic<bool>       drive_changed_{false};
    Vr41xxRtcxTicks         rtcx_{emu_, Vr41xxRtcxDomain::Peripheral};

    bool SequencerFrozen() const override;
    bool SequencerTouch() override;
    void SequencerTouchPulse() override;
    bool SequencerScanLinesNone() const override;
    void SequencerScanEntered() override;
    void SequencerScanDataStored() override;
    [[noreturn]] void SequencerHalt(const char* op, uint16_t scanrep) override;

    struct KscanDrive {
        uint16_t released  = 0;
        uint16_t undefined = 0;
        uint16_t driven    = 0;
        uint16_t high      = 0;
    };
    KscanDrive KscanDriveMasksLocked() const;
    std::optional<PinDrive> KscanDriveLocked(int gpio);
    uint64_t DriveSignatureLocked() const;
    void NoteDriveLocked();
    void FlushDriveChange();
    bool TimedLocked() const;
    uint16_t PiodLocked() const;
    bool TouchLocked() const;
    void LatchDueSlotsLocked(uint64_t now, uint16_t piod);
    void EnterUndefinedLocked();
    uint8_t DrivenKportLocked();
    void NotifyKportLocked(uint8_t kport);
    void PublishKportLocked(bool force);
    void PulseKportLocked(uint16_t lines, uint16_t piod);
    void ArmScanLocked(uint64_t start);
    void AdvancePhaseLocked();
    void AdvancePhaseLocked(uint16_t piod);
    void ArmEventLocked();
    void UpdateLocked();
    void ApplyHostKeysLocked();
    uint32_t ScanPeriodTicksLocked();
    void PublishCausesLocked();
    void ApplyResetLocked();
    void RequireTclockIfTimedLocked(const char* operation);
};

}
