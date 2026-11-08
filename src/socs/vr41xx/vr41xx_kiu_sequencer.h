#pragma once

#include "vr41xx_kiu_regs.h"

#include <cstdint>

namespace cerfcore {

class StateReader;
class StateWriter;

class Vr41xxKiuSequencerHost {
public:
    virtual bool SequencerFrozen() const = 0;
    virtual bool SequencerTouch() = 0;
    virtual void SequencerTouchPulse() = 0;
    virtual bool SequencerScanLinesNone() const = 0;
    virtual void SequencerScanEntered() = 0;
    virtual void SequencerScanDataStored() = 0;
    [[noreturn]] virtual void SequencerHalt(const char* op, uint16_t scanrep) = 0;

protected:
    ~Vr41xxKiuSequencerHost() = default;
};

class Vr41xxKiuSequencer {
public:
    explicit Vr41xxKiuSequencer(Vr41xxKiuSequencerHost& host) : host_(host) {}

    void Configure(uint32_t stprep_zero_count, bool scanstart_auto_clear,
                   bool keyen_scanline_interlock, bool keyen_stop_deferred,
                   bool scanstart_while_running_defined);
    void Reset();

    uint16_t ScanRep() const { return scanrep_; }
    uint16_t State() const { return sstat_; }
    bool     Enabled() const { return (scanrep_ & cerf_vr41xx_kiu_regs::kKeyen) != 0u; }

    void WriteScanRep(uint16_t value);
    void ReevaluateWaitKeyIn();
    void CompleteScan(bool key_data);
    void EnterScanning();

    void Save(StateWriter& w) const;
    void Restore(StateReader& r);

private:
    uint32_t StpRepCount() const;
    void     BeginScan();
    void     EnterWaitKeyIn();
    void     ClearScanStp();

    Vr41xxKiuSequencerHost& host_;
    uint32_t stprep_zero_count_        = 0;
    bool     scanstart_auto_clear_     = false;
    bool     keyen_scanline_interlock_ = false;
    bool     keyen_stop_deferred_      = false;
    bool     scanstart_while_running_defined_ = false;
    uint16_t scanrep_    = cerf_vr41xx_kiu_regs::kScanRepPowerOn;
    uint16_t sstat_       = cerf_vr41xx_kiu_regs::kSStatStopped;
    uint16_t zero_scans_  = 0;
    bool     stop_after_scan_             = false;
    bool     scanstart_set_while_running_ = false;
    bool     scanstp_set_in_waitkeyin_    = false;
    bool     keyen_reenabled_in_scan_     = false;
};

}
