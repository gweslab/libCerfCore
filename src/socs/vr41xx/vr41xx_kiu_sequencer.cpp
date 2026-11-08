#include "vr41xx_kiu_sequencer.h"

#include <cerfcore/state/state_stream.h>

namespace cerfcore {

using namespace cerf_vr41xx_kiu_regs;

void Vr41xxKiuSequencer::Configure(uint32_t stprep_zero_count, bool scanstart_auto_clear,
                                   bool keyen_scanline_interlock, bool keyen_stop_deferred,
                                   bool scanstart_while_running_defined) {
    stprep_zero_count_        = stprep_zero_count;
    scanstart_auto_clear_     = scanstart_auto_clear;
    keyen_scanline_interlock_ = keyen_scanline_interlock;
    keyen_stop_deferred_      = keyen_stop_deferred;
    scanstart_while_running_defined_ = scanstart_while_running_defined;
}

void Vr41xxKiuSequencer::Reset() {
    scanrep_    = kScanRepPowerOn;
    sstat_      = kSStatStopped;
    zero_scans_ = 0;
    stop_after_scan_             = false;
    scanstart_set_while_running_ = false;
    scanstp_set_in_waitkeyin_    = false;
    keyen_reenabled_in_scan_     = false;
}

uint32_t Vr41xxKiuSequencer::StpRepCount() const {
    const uint32_t stprep = (scanrep_ >> kStpRepShift) & kStpRepBits;
    return stprep != 0 ? stprep : stprep_zero_count_;
}

void Vr41xxKiuSequencer::BeginScan() {
    sstat_ = kSStatScanning;
    if (scanstart_auto_clear_ && !scanstart_set_while_running_)
        scanrep_ &= static_cast<uint16_t>(~kScanStart);
    host_.SequencerScanEntered();
}

void Vr41xxKiuSequencer::EnterWaitKeyIn() {
    sstat_ = kSStatWaitKeyIn;
    if (!(scanrep_ & kAtScan) || !host_.SequencerTouch()) return;
    host_.SequencerTouchPulse();
    BeginScan();
}

void Vr41xxKiuSequencer::ReevaluateWaitKeyIn() {
    if (!host_.SequencerFrozen() && Enabled() && sstat_ <= kSStatWaitKeyIn) EnterWaitKeyIn();
}

void Vr41xxKiuSequencer::ClearScanStp() {
    scanrep_ &= static_cast<uint16_t>(~kScanStp);
    EnterWaitKeyIn();
}

void Vr41xxKiuSequencer::EnterScanning() { sstat_ = kSStatScanning; }

void Vr41xxKiuSequencer::CompleteScan(bool key_data) {
    keyen_reenabled_in_scan_ = false;
    host_.SequencerScanDataStored();

    if (key_data) zero_scans_ = 0;
    else          ++zero_scans_;

    if (stop_after_scan_) {
        stop_after_scan_ = false;
        sstat_ = kSStatStopped;
        return;
    }
    if (scanrep_ & kScanStp) {
        if (scanstp_set_in_waitkeyin_) EnterWaitKeyIn();
        else                           ClearScanStp();
        return;
    }
    if (scanrep_ & kAtStp) {
        if (zero_scans_ >= StpRepCount()) {
            zero_scans_ = 0;
            EnterWaitKeyIn();
            return;
        }
    }
    sstat_ = kSStatInterval;
}

void Vr41xxKiuSequencer::WriteScanRep(uint16_t value) {
    const uint16_t previous = scanrep_;
    scanrep_ = value & kScanRepMask;
    const bool scanstart_set = (scanrep_ & kScanStart) != 0 && (previous & kScanStart) == 0;
    scanstart_set_while_running_ = (scanrep_ & kScanStart) != 0 &&
                                   (sstat_ == kSStatInterval || sstat_ == kSStatScanning);
    scanstp_set_in_waitkeyin_ = (scanrep_ & kScanStp) != 0 && sstat_ <= kSStatWaitKeyIn;
    const uint16_t state_at_write = sstat_;
    const bool     follows_keyen_rise_in_scan = keyen_reenabled_in_scan_;
    keyen_reenabled_in_scan_ = false;

    if ((scanrep_ & kAtStp) && StpRepCount() == 0)
        host_.SequencerHalt("KIU ATSTP with an RFU STPREP count", scanrep_);

    if (host_.SequencerScanLinesNone() && (scanrep_ & kKeyen)) {
        if (!keyen_scanline_interlock_)
            host_.SequencerHalt("KIU KEYEN set while SCANLINE = 11 on a chip documenting no "
                                "interlock", scanrep_);
        scanrep_ &= static_cast<uint16_t>(~kKeyen);
    }

    if (!Enabled()) {
        if (sstat_ == kSStatScanning) {
            if (!keyen_stop_deferred_)
                host_.SequencerHalt("KIU KEYEN cleared during a scan on a chip documenting no "
                                    "deferral", scanrep_);
            stop_after_scan_ = true;
        } else {
            sstat_ = kSStatStopped;
        }
        return;
    }
    if ((scanrep_ & kScanStp) && (scanrep_ & kScanStart) && (!scanstart_while_running_defined_ || scanstart_set))
        host_.SequencerHalt("KIU SCANSTP and SCANSTART set together", scanrep_);

    stop_after_scan_ = false;
    ReevaluateWaitKeyIn();
    if ((previous & kKeyen) == 0 && sstat_ == kSStatScanning) keyen_reenabled_in_scan_ = true;
    if ((scanrep_ & kScanStp) && sstat_ == kSStatInterval) {
        ClearScanStp();
        return;
    }
    if ((scanrep_ & kScanStart) == 0 || (scanrep_ & kScanStp)) return;
    if (state_at_write == kSStatScanning &&
        !(scanstart_while_running_defined_ && scanstart_set && follows_keyen_rise_in_scan))
        host_.SequencerHalt("KIU SCANSTART during a scan", scanrep_);
    if (sstat_ != kSStatScanning) BeginScan();
}

void Vr41xxKiuSequencer::Save(StateWriter& w) const {
    w.Write("scanrep", scanrep_);
    w.Write("sstat", sstat_);
    w.Write("zero_scans", zero_scans_);
    w.Write("stop_after_scan", stop_after_scan_);
    w.Write("scanstart_set_while_running", scanstart_set_while_running_);
    w.Write("scanstp_set_in_waitkeyin", scanstp_set_in_waitkeyin_);
    w.Write("keyen_reenabled_in_scan", keyen_reenabled_in_scan_);
}

void Vr41xxKiuSequencer::Restore(StateReader& r) {
    r.Read("scanrep", scanrep_);
    r.Read("sstat", sstat_);
    r.Read("zero_scans", zero_scans_);
    r.Read("stop_after_scan", stop_after_scan_);
    r.Read("scanstart_set_while_running", scanstart_set_while_running_);
    r.Read("scanstp_set_in_waitkeyin", scanstp_set_in_waitkeyin_);
    r.Read("keyen_reenabled_in_scan", keyen_reenabled_in_scan_);
}

}
