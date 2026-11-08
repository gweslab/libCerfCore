#pragma once

#include "rcvdl56dpfl_configuration.h"
#include "rcvdl56dpfl_far_end_link.h"
#include "rcvdl56dpfl_receiver.h"
#include "rcvdl56dpfl_transmitter.h"

#include <cstdint>
#include <vector>

namespace cerfcore {

class Rcvdl56dpflDspRam;
class StateReader;
class StateWriter;

class Rcvdl56dpflLine {
public:
    using Registers = Rcvdl56dpflConfiguration::Registers;

    explicit Rcvdl56dpflLine(const Rcvdl56dpflDspRam& ram) : ram_(&ram) {}

    Registers&       Regs() { return regs_; }
    const Registers& Regs() const { return regs_; }
    uint8_t          ActiveConf() const { return active_conf_; }

    void Reset(const Registers& defaults);
    void LatchConfiguration();
    void ConnectRemote(uint8_t conf, uint8_t speed);
    void DisconnectRemote();
    void ReportCleardown(const Rcvdl56dpflCleardown& cleardown);
    void                 QueueRemoteBytes(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> TakeOutbound() { return far_end_.TakeOutbound(); }
    bool                 TakeRemoteDrained() { return far_end_.TakeDrained(); }
    bool CarrierWanted() const;
    Rcvdl56dpflLineConfig LineConfig() const;

    void CatchUp(uint64_t elapsed, uint64_t now);
    void RefreshLine();
    const char* QueueTransmitByte(uint8_t value);
    void SetTransmitBufferEmpty(bool empty);
    void HostReadReceiveBuffer();

    void SetStatus(uint32_t reg, uint8_t value);
    void NoteNewStatus();
    void UpdateNewStatusInterrupt();
    void UpdateBufferInterrupts();
    bool IrqAsserted() const;
    uint64_t InterruptRiseTick() const;

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    bool InternalLoop() const;
    uint64_t NextEventTick() const;
    void ProcessDueEvent();
    void DeliverFarEnd(uint64_t tick);
    bool RateChangeDue(uint32_t reg, uint8_t bit, bool renegotiation) const;
    void CompleteRateChange(uint32_t reg, uint8_t bit, bool renegotiation);
    void ApplyTxOut(const Rcvdl56dpflLineConfig& c, const Rcvdl56dpflTxOut& out);
    void PublishLineStatus();
    void UpdateClearToSend();
    void UpdateLoopStatus();
    uint8_t WaitingInterrupts() const;
    uint8_t RaisedInterrupts() const;
    bool SameStateAs(const Rcvdl56dpflLine& other) const;

    const Rcvdl56dpflDspRam* ram_;
    Registers                regs_{};
    Rcvdl56dpflTransmitter   transmitter_;
    Rcvdl56dpflReceiver      receiver_;
    uint64_t now_          = 0u;
    uint8_t  active_conf_  = 0u;
    bool     loop3_        = false;
    Rcvdl56dpflFarEndLink far_end_;
    bool     v21_sync_     = false;
    bool     cleared_down_ = false;
};

}
