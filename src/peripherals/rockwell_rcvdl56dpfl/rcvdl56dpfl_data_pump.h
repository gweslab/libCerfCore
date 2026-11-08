#pragma once

#include "rcvdl56dpfl_call_progress.h"
#include "rcvdl56dpfl_configuration.h"
#include "rcvdl56dpfl_data_pump_host.h"
#include "rcvdl56dpfl_dsp_ram.h"
#include "rcvdl56dpfl_handshake.h"
#include "rcvdl56dpfl_line.h"
#include "rcvdl56dpfl_self_test.h"

#include <cstdint>

namespace cerfcore {

class PhoneLine;
class StateReader;
class StateWriter;

class Rcvdl56dpflDataPump {
public:
    Rcvdl56dpflDataPump(Rcvdl56dpflDataPumpHost& host, PhoneLine& phone);

    uint8_t     Read(uint32_t reg);
    const char* Write(uint32_t reg, uint8_t value);

    void SetSelfTestResults(const Rcvdl56dpflSelfTest& results) { self_test_ = &results; }
    void SetResetPin(bool high);
    void TimerElapsed();
    void RearmTimer();
    void ServiceHostData();

    void SaveState(StateWriter& w) const;
    void RestoreState(StateReader& r);

private:
    enum class SelfTestPhase : uint8_t { kNone, kController, kDsp };

    void PowerOn();
    void Reset();
    void StartSelfTest();
    void AdvanceSelfTest();
    void EndSelfTestWindow(int64_t at_ns);
    void LoadSelfTestWord(uint32_t high_reg, uint32_t low_reg, uint16_t value);
    const char* WriteDuringSelfTest(uint32_t reg, uint8_t value);
    bool InSelfTest() const { return self_test_phase_ != SelfTestPhase::kNone; }
    void PublishIrq();
    void CatchUp();
    void ExchangeRemote();
    void SyncBitRate();
    const char* WriteRegister(uint32_t reg, uint8_t value);
    const char* WriteNewStatusRegister(uint8_t value);
    void WriteBufferStatusRegister(uint8_t value);
    const char* WriteMemoryAccessRegister(uint8_t value);
    const char* WriteTransmitBuffer(uint8_t value);
    void WriteDownloadControl(uint8_t value);
    void WriteModeRegister(uint8_t value);
    void WriteControl02(uint8_t value);
    void WriteLoopbackRegister(uint8_t value);
    void UpdateToneStatus();
    const char* AdvanceHandshake();
    const char* ProcessNewConfiguration();
    const char* ProcessRamAccess();
    uint32_t RamCode() const;

    Rcvdl56dpflDataPumpHost& host_;
    PhoneLine&               phone_;
    Rcvdl56dpflCallProgress  call_progress_;
    Rcvdl56dpflHandshake     handshake_;
    Rcvdl56dpflDspRam        ram_;
    Rcvdl56dpflLine          line_{ram_};
    const Rcvdl56dpflSelfTest* self_test_ = nullptr;
    SelfTestPhase self_test_phase_  = SelfTestPhase::kNone;
    int64_t       self_test_end_ns_ = 0;
    bool reset_held_  = false;
    bool asleep_      = false;
    bool downloading_ = false;
    bool irq_level_   = false;
    uint8_t  download_mode_  = 0u;
    uint32_t download_crc_   = 0u;
    uint32_t download_bytes_ = 0u;
};

}
