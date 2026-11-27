#pragma once

#include "oscillator_ticks.h"

#include <cstdint>
#include <span>

namespace cerfcore {

class CerfEmulator;
class StateReader;
class StateWriter;
class UlpiPhy;
class UsbDevice;

struct ChipIdeaUsbCoreRegValue {
    uint32_t off;
    uint32_t value;
};

struct ChipIdeaUsbCoreKind {
    uint32_t                                 usbcmd_born;
    uint32_t                                 portsc_born;
    bool                                     host_only;
    std::span<const ChipIdeaUsbCoreRegValue> power_on;
};

enum class ChipIdeaPortscWrite {
    kRejected,
    kApplied,
    kDeviceReset,
};

class ChipIdeaUsbCore {
public:
    static constexpr uint32_t kBlockBytes = 0x200u;
    static constexpr uint64_t kNever      = ~0ull;

    static constexpr uint32_t kUsbcmd          = 0x140u;
    static constexpr uint32_t kUsbsts          = 0x144u;
    static constexpr uint32_t kUsbintr         = 0x148u;
    static constexpr uint32_t kFrindex         = 0x14Cu;
    static constexpr uint32_t kCtrlDsSegment   = 0x150u;
    static constexpr uint32_t kPeriodicList    = 0x154u;
    static constexpr uint32_t kListAddr        = 0x158u;
    static constexpr uint32_t kTxFillTuning    = 0x164u;
    static constexpr uint32_t kUlpiViewport    = 0x170u;
    static constexpr uint32_t kConfigFlag      = 0x180u;
    static constexpr uint32_t kPortsc          = 0x184u;
    static constexpr uint32_t kOtgsc           = 0x1A4u;
    static constexpr uint32_t kUsbmode         = 0x1A8u;
    static constexpr uint32_t kEndptSetupStat  = 0x1ACu;
    static constexpr uint32_t kEndptPrime      = 0x1B0u;
    static constexpr uint32_t kEndptFlush      = 0x1B4u;
    static constexpr uint32_t kEndptStat       = 0x1B8u;
    static constexpr uint32_t kEndptComplete   = 0x1BCu;
    static constexpr uint32_t kEndptCtrl0      = 0x1C0u;
    static constexpr uint32_t kEndpoints       = 16u;

    static constexpr uint32_t kUsbcmdRunStop = 1u << 0;
    static constexpr uint32_t kUsbcmdReset   = 1u << 1;
    static constexpr uint32_t kUsbcmdPse     = 1u << 4;
    static constexpr uint32_t kUsbcmdAse     = 1u << 5;

    static constexpr uint32_t kUsbstsUi       = 1u << 0;
    static constexpr uint32_t kUsbstsUei      = 1u << 1;
    static constexpr uint32_t kUsbstsPci      = 1u << 2;
    static constexpr uint32_t kUsbstsUlpii    = 1u << 10;
    static constexpr uint32_t kUsbstsHalted   = 1u << 12;
    static constexpr uint32_t kUsbstsReclaim  = 1u << 13;
    static constexpr uint32_t kUsbstsPeriodic = 1u << 14;
    static constexpr uint32_t kUsbstsAsync    = 1u << 15;
    static constexpr uint32_t kUsbstsReadOnly =
        kUsbstsHalted | kUsbstsReclaim | kUsbstsPeriodic | kUsbstsAsync;

    static constexpr uint32_t kOtgscReadWrite   = 0x7F00003Bu;
    static constexpr uint32_t kOtgscIdB         = 1u << 8;
    static constexpr uint32_t kOtgscAvv         = 1u << 9;
    static constexpr uint32_t kOtgscAsv         = 1u << 10;
    static constexpr uint32_t kOtgscBsv         = 1u << 11;
    static constexpr uint32_t kOtgscBse         = 1u << 12;
    static constexpr uint32_t kOtgscOneMsToggle = 1u << 13;
    static constexpr uint32_t kOtgscInputs      = kOtgscIdB | kOtgscAvv | kOtgscAsv | kOtgscBsv | kOtgscBse;
    static constexpr uint32_t kOtgscIdis        = 1u << 16;
    static constexpr uint32_t kOtgscAvvis       = 1u << 17;
    static constexpr uint32_t kOtgscAsvis       = 1u << 18;
    static constexpr uint32_t kOtgscBsvis       = 1u << 19;
    static constexpr uint32_t kOtgscBseis       = 1u << 20;
    static constexpr uint32_t kOtgscOneMsStatus = 1u << 21;
    static constexpr uint32_t kOtgscLatched     = kOtgscIdis | kOtgscAvvis | kOtgscAsvis | kOtgscBsvis | kOtgscBseis;
    static constexpr uint32_t kOtgscStatusShift = 16u;
    static constexpr uint32_t kOtgscEnableShift = 24u;
    static constexpr uint32_t kOtgscEventMask   = 0x7Fu;
    static constexpr uint32_t kOtgscOneMsEnable = 1u << 29;

    static constexpr uint32_t kOtgMirrorInputs = 0x1Fu;

    static constexpr uint32_t kUsbmodeCm     = 0x3u;
    static constexpr uint32_t kUsbmodeDevice = 0x2u;
    static constexpr uint32_t kUsbmodeHost   = 0x3u;

    static constexpr uint32_t kHcsparamsPpc = 1u << 4;
    static constexpr uint32_t kHcsparamsPi  = 1u << 16;

    static constexpr uint32_t kPortscCcs      = 1u << 0;
    static constexpr uint32_t kPortscPe       = 1u << 2;
    static constexpr uint32_t kPortscHsp      = 1u << 9;
    static constexpr uint32_t kPortscLineDm   = 1u << 10;
    static constexpr uint32_t kPortscLineDp   = 1u << 11;
    static constexpr uint32_t kPortscPp       = 1u << 12;
    static constexpr uint32_t kPortscPhcd     = 1u << 23;
    static constexpr uint32_t kPortscPfsc     = 1u << 24;
    static constexpr uint32_t kPortscPspdShift = 26u;
    static constexpr uint32_t kPortscPspdMask  = 3u << kPortscPspdShift;
    static constexpr uint32_t kPortscPspdHs    = 2u << kPortscPspdShift;
    static constexpr uint32_t kPortscPtsShift = 30u;
    static constexpr uint32_t kPortscPtsMask  = 3u << kPortscPtsShift;
    static constexpr uint32_t kPortscPtsUlpi  = 2u << kPortscPtsShift;
    static constexpr uint32_t kPortscPtsSerial = 3u << kPortscPtsShift;

    ChipIdeaUsbCore(CerfEmulator& emu, uint32_t* block, ChipIdeaUsbCoreKind kind, UlpiPhy* phy);

    void Attach();
    void AttachPhy(UlpiPhy* phy);
    void Rescale();

    uint32_t Reg(uint32_t off) const;
    bool     DeviceMode() const;
    bool     HostMode() const;

    void PowerOn();
    void ControllerReset();

    void WriteUsbcmd(uint32_t value);
    bool ResetPending() const;

    uint32_t Usbsts() const;
    void     WriteUsbsts(uint32_t value);
    void     SetReclamation(bool on);

    uint32_t Otgsc();
    void     WriteOtgsc(uint32_t value);
    void     SampleOtgInputs(bool latch);
    void     ReceivePhyEvent();
    bool     OtgEventPending() const { return otg_event_pending_; }
    bool     UlpiClockRuns() const;
    uint32_t OtgMirror() const { return mirror_; }
    void     WriteOtgMirror(uint32_t value);
    bool     OtgInterrupt();
    uint64_t NextOtgInterruptCycle();

    uint32_t Portsc() const;

    void SaveState(StateWriter& w);
    void RestoreState(StateReader& r);

    void WriteUsbmode(uint32_t value);
    void WriteListAddr(uint32_t value);
    void WriteEndptCtrl(uint32_t endpoint, uint32_t value);
    void ClearOnWrite(uint32_t off, uint32_t value);

    ChipIdeaPortscWrite WritePortsc(uint32_t value, const UsbDevice* attached);
    bool                ReportPortConnect(bool attached);

    bool StoreViewport(uint32_t value);
    bool RunViewport();
    bool WriteViewport(uint32_t value);
    bool ReadViewport(uint32_t& value) const;

    static bool     IsEndptCtrl(uint32_t off);
    static uint32_t EndpointOf(uint32_t off);

private:
    uint32_t& At(uint32_t off);
    bool      Synchronous() const;
    uint32_t  BornMode() const;
    uint32_t  OtgInputs() const;
    bool      OneMsElapsed();
    void      ResumeOtgInputs();

    uint32_t*           block_;
    ChipIdeaUsbCoreKind kind_;
    UlpiPhy*            phy_;
    OscillatorTicks     millis_;
    uint64_t            millis_mark_       = 0;
    uint32_t            mirror_            = 0;
    bool                otg_event_pending_ = false;
};

}
