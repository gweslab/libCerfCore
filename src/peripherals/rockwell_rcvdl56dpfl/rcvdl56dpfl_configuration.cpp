#include "rcvdl56dpfl_configuration.h"

#include "rcvdl56dpfl_dsp_ram.h"
#include "rcvdl56dpfl_registers.h"

#include <algorithm>
#include <iterator>

namespace cerfcore {

namespace {

using namespace rcvdl56dpfl_reg;

struct ConfRate {
    uint8_t  conf;
    uint32_t bit_rate;
};

constexpr ConfRate kConfRates[] = {
    {0xCEu, 33600u}, {0xCDu, 31200u}, {0xCCu, 28800u}, {0xCBu, 26400u}, {0xCAu, 24000u}, {0xC9u, 21600u},
    {0xC8u, 19200u}, {0xC7u, 16800u}, {0xC6u, 14400u}, {0xC5u, 12000u}, {0xC4u, 9600u},  {0xC3u, 7200u},
    {0xC2u, 4800u},  {0xC1u, 2400u},  {0x31u, 14400u}, {0x32u, 12000u}, {0x34u, 9600u},  {0x38u, 7200u},
    {0x76u, 14400u}, {0x72u, 12000u}, {0x74u, 9600u},  {0x75u, 9600u},  {0x78u, 7200u},  {0x71u, 4800u},
    {0xB1u, 14400u}, {0xB2u, 12000u}, {0xB4u, 9600u},  {0xB8u, 7200u},  {0x14u, 9600u},  {0x12u, 7200u},
    {0x11u, 4800u},  {0x02u, 4800u},  {0x01u, 2400u},  {0x84u, 2400u},  {0x82u, 1200u},  {0x52u, 1200u},
    {0x51u, 600u},   {0xA0u, 300u},   {0xA8u, 300u},   {0x23u, 4800u},  {0x62u, 1200u},  {0x60u, 300u},
    {0xA4u, 1200u},  {0xA1u, 75u},
};

constexpr uint8_t kFaxConfs[] = {
    0x31u, 0x32u, 0x34u, 0x38u, 0xB1u, 0xB2u, 0xB4u, 0xB8u, 0x14u, 0x12u, 0x11u, 0x02u, 0x01u, 0xA8u,
};

constexpr uint8_t kFskDataStateConfs[] = {0xA0u, 0xA4u, 0xA1u, 0x60u};
constexpr uint8_t kV25AnswerToneConfs[] = {
    0xAAu, 0x76u, 0x72u, 0x74u, 0x75u, 0x78u, 0x71u, 0x84u, 0x82u, 0x52u,
    0x51u, 0xA4u, 0xA1u, 0xA0u, 0xA8u, 0x80u, 0x81u, 0x83u,
};
constexpr uint8_t kK56flexFirstConf = 0x91u;
constexpr uint8_t kK56flexLastConf  = 0x9Du;
constexpr uint8_t kV22FamilyConfs[]    = {0x84u, 0x82u, 0x52u, 0x51u, 0x62u};
constexpr uint8_t kV32Confs[]          = {0x76u, 0x72u, 0x74u, 0x75u, 0x78u, 0x71u};
constexpr uint8_t kV22bisConfs[]       = {0x84u, 0x82u};

enum class RetrainMode : uint8_t { kNone, kV34, kV32, kV22bis };

struct SpeedRate {
    uint32_t bit_rate;
    uint8_t  code;
};

constexpr SpeedRate kSpeedCodes[] = {
    {600u, 0x01u},   {1200u, 0x02u},  {2400u, 0x03u},  {4800u, 0x04u},  {9600u, 0x05u},  {12000u, 0x06u},
    {14400u, 0x07u}, {7200u, 0x08u},  {16800u, 0x09u}, {19200u, 0x0Au}, {21600u, 0x0Bu}, {24000u, 0x0Cu},
    {26400u, 0x0Du}, {28800u, 0x0Eu}, {31200u, 0x0Fu}, {33600u, 0x10u},
};
constexpr uint8_t kSpeedUpTo300 = 0x00u;

constexpr uint8_t kV32Cleardown = 0x70u;
constexpr uint8_t kV34Cleardown = 0xC0u;
constexpr uint8_t kV8Cleardown  = 0xAFu;

constexpr std::array<uint8_t, 32> kUnmodeledBits = {
    0x00u, 0x38u, 0x18u, 0x64u, 0xEEu, 0xE7u, 0xA0u, 0x14u, 0x00u, 0x42u, 0x00u, 0x04u, 0x00u, 0x05u, 0x00u, 0x00u,
    0x00u, 0x9Cu, 0x00u, 0x0Cu, 0x00u, 0x40u, 0x00u, 0x00u, 0x00u, 0x00u, 0x7Fu, 0x00u, 0x00u, 0x00u, 0x12u, 0x22u,
};

constexpr std::array<uint8_t, 4> kWdszDataBits = {5u, 7u, 6u, 8u};
constexpr std::array<uint8_t, 4> kRxTriggers   = {1u, 4u, 8u, 14u};

constexpr uint32_t kRxFifoControl   = 0x32Cu;
constexpr uint32_t kRxFifoExtension = 0x701u;
constexpr uint32_t kTxFifoExtension = 0x702u;
constexpr uint32_t kHdlcAbortCode   = 0x3A5u;
constexpr uint8_t  kAbortBit        = 0x40u;
constexpr uint32_t kCrcSelect       = 0x0B3u;
constexpr uint16_t kCrc16Selected   = 0x0001u;
constexpr uint16_t kFcsTransmitDisabled = 0x0040u;
constexpr uint8_t  kConfV21         = 0xA0u;

bool Listed(const uint8_t* begin, const uint8_t* end, uint8_t conf) {
    return std::find(begin, end, conf) != end;
}

RetrainMode ModeOf(uint8_t conf) {
    if (conf >= 0xC1u && conf <= 0xCEu) return RetrainMode::kV34;
    if (Listed(std::begin(kV32Confs), std::end(kV32Confs), conf)) return RetrainMode::kV32;
    if (Listed(std::begin(kV22bisConfs), std::end(kV22bisConfs), conf)) return RetrainMode::kV22bis;
    return RetrainMode::kNone;
}

}

uint32_t Rcvdl56dpflConfiguration::BitRate(uint8_t conf) {
    for (const ConfRate& c : kConfRates)
        if (c.conf == conf) return c.bit_rate;
    return 0u;
}

bool Rcvdl56dpflConfiguration::Fax(uint8_t conf) {
    return Listed(std::begin(kFaxConfs), std::end(kFaxConfs), conf);
}

bool Rcvdl56dpflConfiguration::FskDataState(uint8_t conf) {
    return Listed(std::begin(kFskDataStateConfs), std::end(kFskDataStateConfs), conf);
}

bool Rcvdl56dpflConfiguration::V22Family(uint8_t conf) {
    return Listed(std::begin(kV22FamilyConfs), std::end(kV22FamilyConfs), conf);
}

bool Rcvdl56dpflConfiguration::Retrains(uint8_t conf) {
    return ModeOf(conf) != RetrainMode::kNone;
}

bool Rcvdl56dpflConfiguration::RenegotiatesRate(uint8_t conf) {
    const RetrainMode mode = ModeOf(conf);
    return mode == RetrainMode::kV34 || mode == RetrainMode::kV32;
}

bool Rcvdl56dpflConfiguration::ReportsV25AnswerTone(uint8_t conf) {
    if (conf >= kK56flexFirstConf && conf <= kK56flexLastConf) return true;
    return Listed(std::begin(kV25AnswerToneConfs), std::end(kV25AnswerToneConfs), conf);
}

bool Rcvdl56dpflConfiguration::V8OrV34(uint8_t conf) {
    return conf == kConfV8 || ModeOf(conf) == RetrainMode::kV34;
}

bool Rcvdl56dpflConfiguration::SameRetrainMode(uint8_t a, uint8_t b) {
    return ModeOf(a) != RetrainMode::kNone && ModeOf(a) == ModeOf(b);
}

Rcvdl56dpflCleardown Rcvdl56dpflConfiguration::CleardownOf(uint8_t conf) {
    switch (ModeOf(conf)) {
        case RetrainMode::kV32:    return {true, kV32Cleardown, false};
        case RetrainMode::kV34:    return {true, kV34Cleardown, true};
        case RetrainMode::kV22bis:
        case RetrainMode::kNone:   break;
    }
    return {};
}

Rcvdl56dpflCleardown Rcvdl56dpflConfiguration::V8Cleardown() {
    return {true, kV8Cleardown, false};
}

uint8_t Rcvdl56dpflConfiguration::UnmodeledBits(uint32_t reg) {
    return kUnmodeledBits[reg];
}

uint8_t Rcvdl56dpflConfiguration::SpeedCode(uint32_t bit_rate) {
    for (const SpeedRate& s : kSpeedCodes)
        if (s.bit_rate == bit_rate) return s.code;
    return kSpeedUpTo300;
}

bool Rcvdl56dpflConfiguration::RemoteLoopRequested(const Registers& regs, uint8_t conf) {
    return V22Family(conf) && (regs[kLoopback] & kRdl) != 0u;
}

Rcvdl56dpflLineConfig Rcvdl56dpflConfiguration::Line(const Registers& regs, const Rcvdl56dpflDspRam& ram,
                                                     uint8_t conf, bool internal_loop, bool v21_sync) {
    Rcvdl56dpflLineConfig c;
    const uint8_t format = regs[kFormat];
    c.hdlc          = (format & kHdlc) != 0u;
    const uint16_t crc_select = ram.Read(kCrcSelect);
    c.fcs_bytes     = (crc_select & kCrc16Selected) ? Rcvdl56dpflFrameCheck::kFcs16Bytes
                                                    : Rcvdl56dpflFrameCheck::kFcs32Bytes;
    c.fcs_sent      = (crc_select & kFcsTransmitDisabled) == 0u;
    c.async         = conf == kConfV21 ? !v21_sync : (regs[kTransmit] & kAsyn) != 0u;
    c.data_bits     = kWdszDataBits[format & kWdszMask];
    c.parity        = (format & kPen) != 0u;
    c.stop_bits     = (format & kStb) ? 2u : 1u;
    c.parity_select = static_cast<uint8_t>((regs[kControl11] >> 5) & 0x03u);
    c.tx_fifo          = (regs[kControl04] & kFifoen) != 0u;
    c.tx_fifo_extended = (ram.Read(kTxFifoExtension) & 1u) != 0u;
    c.rx_fifo_extended = (ram.Read(kRxFifoExtension) & 1u) != 0u;
    c.abort_on_underrun = (ram.Read(kHdlcAbortCode) & kAbortBit) != 0u;
    c.cts       = (regs[kStatus0F] & kCts) != 0u;
    c.parallel  = (regs[kTransmit] & kTpdm) != 0u;
    c.mark_hold = (regs[kLoopback] & kMhld) != 0u;
    const bool remote_loop = RemoteLoopRequested(regs, conf);
    const bool marks = c.mark_hold || (regs[kLoopback] & kL2act) || !c.parallel || remote_loop;
    c.takes_tbuffer = c.cts && !marks;
    c.send_511      = (regs[kControl02] & kS511) != 0u;
    c.v54_pattern   = static_cast<uint8_t>(regs[kTransmit] & kV54Sends);
    c.v54_detectors = static_cast<uint8_t>(regs[kControl02] & kV54Enables);
    c.loop_listens  = internal_loop && !(regs[kControl05] & kTxsq) && (regs[kControl03] & kRlsde) && !remote_loop;
    const uint16_t fifo = ram.Read(kRxFifoControl);
    c.rx_trigger      = kRxTriggers[(fifo >> 6) & 0x03u];
    c.rx_timeout      = (fifo & 0x02u) != 0u;
    c.rx_timeout_bits = static_cast<uint8_t>(9u + 4u * ((fifo >> 2) & 0x07u));
    return c;
}

}
