#include "rcvdl56dpfl_handshake.h"

#include "../phone_line/phone_line.h"
#include "rcvdl56dpfl_configuration.h"
#include "rcvdl56dpfl_dsp_ram.h"
#include "rcvdl56dpfl_line.h"
#include "rcvdl56dpfl_registers.h"

#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

namespace cerfcore {

namespace {

using namespace rcvdl56dpfl_reg;

constexpr uint32_t kV34Conf        = 0x309u;
constexpr uint32_t kV34RateMask    = 0x382u;
constexpr uint32_t kBaudRateMask   = 0x101u;
constexpr uint32_t kTxRateCap      = 0x605u;
constexpr uint32_t kRxRateCap      = 0x604u;
constexpr uint32_t kCallMenuV34    = 0x306u;
constexpr uint8_t  kCallMenuV34Fdx = 0x40u;
constexpr uint32_t kCallFunctionTx = 0x304u;
constexpr uint8_t  kCallFunction   = 0xE0u;
constexpr uint32_t kV8Received     = 0x302u;
constexpr uint8_t  kAnsamDetected  = 0x08u;
constexpr uint8_t  kNoModesCommon  = 0x01u;
constexpr uint32_t kV8Protocol     = 0x303u;
constexpr uint32_t kRemoteRatesLow  = 0x208u;
constexpr uint32_t kRemoteRatesHigh = 0x209u;
constexpr uint32_t kSymbolRateValue = 0x2E3u;
constexpr uint32_t kV34RxSpeed      = 0x2E4u;
constexpr uint32_t kV34TxSpeed      = 0x2E5u;

constexpr uint8_t  kConfV34Base    = 0xC0u;
constexpr uint8_t  kConfV34First   = 0xC1u;
constexpr uint8_t  kConfV34Last    = 0xCEu;
constexpr uint8_t  kConfK56First   = 0x91u;
constexpr uint8_t  kConfK56Last    = 0x9Du;
constexpr uint8_t  kRateCapLast    = 0x0Eu;
constexpr uint32_t kRateStep       = 2400u;
constexpr uint16_t kRateMaskBits   = 0x3FFFu;
constexpr uint32_t kFarEndMaxRate  = 33600u;

constexpr uint16_t kFarEndRates    = 0x3FFFu;

constexpr uint8_t kTxPausing         = 0x00u;
constexpr uint8_t kRxLookingForAnsam = 0x00u;
constexpr uint8_t kTxPhase4Data      = 0x67u;
constexpr uint8_t kRxPhase4Data      = 0x68u;

struct SymbolRateCap {
    uint8_t  brm_bit;
    uint8_t  value;
    uint32_t max_rate;
};

constexpr SymbolRateCap kSymbolRateCaps[] = {
    {0x20u, 5u, 33600u}, {0x10u, 4u, 31200u}, {0x08u, 3u, 28800u}, {0x04u, 2u, 26400u}, {0x01u, 0u, 21600u},
};
constexpr const SymbolRateCap& kNoCommonSymbolRate = kSymbolRateCaps[std::size(kSymbolRateCaps) - 1u];

const SymbolRateCap& NegotiatedSymbolRate(uint8_t brm) {
    for (const SymbolRateCap& s : kSymbolRateCaps)
        if (brm & s.brm_bit) return s;
    return kNoCommonSymbolRate;
}

bool HandshakeEnabled(const Rcvdl56dpflLine& line) {
    return (line.Regs()[kMode] & kData) || (line.Regs()[kPower] & kAuto);
}

}

const char* Rcvdl56dpflHandshake::Refuse(const char* format, unsigned value) {
    std::snprintf(reason_, sizeof(reason_), format, value);
    return reason_;
}

void Rcvdl56dpflHandshake::Reset() {
    state_ = State::kIdle;
}

const char* Rcvdl56dpflHandshake::OnNewConfiguration(Rcvdl56dpflLine& line, PhoneLine& phone) {
    if (state_ == State::kConnected) return "NEWC during a V.34 connection";
    state_ = State::kIdle;
    FollowControls(line, phone);
    return nullptr;
}

void Rcvdl56dpflHandshake::FollowControls(Rcvdl56dpflLine& line, PhoneLine& phone) {
    const auto& regs = line.Regs();
    const bool  dtr  = (regs[kMode] & kDtr) != 0u;
    if (state_ == State::kConnected && (!dtr || !phone.CallUp())) {
        line.DisconnectRemote();
        phone.StopData();
        state_ = State::kIdle;
    }
    if (!dtr) {
        if (state_ != State::kClearedDown) state_ = State::kIdle;
        return;
    }
    if (state_ == State::kIdle && line.ActiveConf() == kConfV8 && (regs[kMode] & kOrg) && HandshakeEnabled(line)) {
        state_ = State::kOriginating;
        line.SetStatus(kSectxb, kTxPausing);
        line.SetStatus(kSecrxb, kRxLookingForAnsam);
    }
}

const char* Rcvdl56dpflHandshake::Advance(Rcvdl56dpflLine& line, Rcvdl56dpflDspRam& ram, PhoneLine& phone,
                                          int64_t now_ns) {
    if (state_ != State::kOriginating) return nullptr;
    phone.Advance(now_ns);
    if (!HandshakeEnabled(line)) return nullptr;
    if (!phone.AnswerToneAt(now_ns)) return nullptr;
    phone.MarkTx();
    phone.MarkRx();
    return Connect(line, ram, phone);
}

const char* Rcvdl56dpflHandshake::Connect(Rcvdl56dpflLine& line, Rcvdl56dpflDspRam& ram, PhoneLine& phone) {
    const uint8_t desired = static_cast<uint8_t>(ram.Read(kV34Conf));
    const uint8_t tx_cap  = static_cast<uint8_t>(ram.Read(kTxRateCap));
    const uint8_t rx_cap  = static_cast<uint8_t>(ram.Read(kRxRateCap));
    uint32_t      limit   = kFarEndMaxRate;
    if (desired >= kConfV34First && desired <= kConfV34Last)
        limit = Rcvdl56dpflConfiguration::BitRate(desired);
    else if (desired < kConfK56First || desired > kConfK56Last)
        return Refuse("RAM 309h holds %02Xh", desired);
    if (tx_cap > kRateCapLast) return Refuse("RAM 605h holds %02Xh", tx_cap);
    if (rx_cap > kRateCapLast) return Refuse("RAM 604h holds %02Xh", rx_cap);
    if (tx_cap != 0u) limit = std::min(limit, tx_cap * kRateStep);
    if (rx_cap != 0u) limit = std::min(limit, rx_cap * kRateStep);
    const uint8_t call_function = static_cast<uint8_t>(ram.Read(kCallFunctionTx) & kCallFunction);
    if (!(ram.Read(kCallMenuV34) & kCallMenuV34Fdx)) {
        ram.Write(kV8Received, static_cast<uint16_t>(call_function | kAnsamDetected | kNoModesCommon));
        ClearDown(line, phone, Rcvdl56dpflConfiguration::V8Cleardown());
        return nullptr;
    }
    ram.Write(kV8Received, static_cast<uint16_t>(call_function | kAnsamDetected));
    ram.Write(kV8Protocol, 0u);
    const SymbolRateCap& symbol = NegotiatedSymbolRate(static_cast<uint8_t>(ram.Read(kBaudRateMask)));
    ram.Write(kSymbolRateValue, symbol.value);
    ram.Write(kRemoteRatesLow, static_cast<uint16_t>(kFarEndRates & 0xFFu));
    ram.Write(kRemoteRatesHigh, static_cast<uint16_t>(kFarEndRates >> 8));
    limit = std::min(limit, symbol.max_rate);
    const uint16_t mask = static_cast<uint16_t>(ram.Read(kV34RateMask) & kRateMaskBits);
    uint32_t       rate = 0u;
    for (uint32_t r = limit - limit % kRateStep; r >= kRateStep && rate == 0u; r -= kRateStep)
        if (mask & (1u << (r / kRateStep - 1u))) rate = r;
    if (rate == 0u) {
        ClearDown(line, phone, Rcvdl56dpflConfiguration::CleardownOf(kConfV34First));
        return nullptr;
    }
    ram.Write(kV34RxSpeed, static_cast<uint16_t>(rate / kRateStep));
    ram.Write(kV34TxSpeed, static_cast<uint16_t>(rate / kRateStep));
    auto& regs = line.Regs();
    if (regs[kPower] & kAuto) {
        regs[kPower] = static_cast<uint8_t>(regs[kPower] & ~kAuto);
        regs[kMode] |= kData;
    }
    line.ConnectRemote(static_cast<uint8_t>(kConfV34Base + rate / kRateStep),
                       Rcvdl56dpflConfiguration::SpeedCode(rate));
    line.SetStatus(kSectxb, kTxPhase4Data);
    line.SetStatus(kSecrxb, kRxPhase4Data);
    phone.StartData();
    state_ = State::kConnected;
    return nullptr;
}

void Rcvdl56dpflHandshake::ClearDown(Rcvdl56dpflLine& line, PhoneLine& phone, const Rcvdl56dpflCleardown& cleardown) {
    line.ReportCleardown(cleardown);
    phone.FarEndHangUp();
    state_ = State::kClearedDown;
}

void Rcvdl56dpflHandshake::SaveState(StateWriter& w) const {
    w.Write<uint8_t>("handshake_state", static_cast<uint8_t>(state_));
}

void Rcvdl56dpflHandshake::RestoreState(StateReader& r) {
    uint8_t state = 0u;
    r.Read("handshake_state", state);
    state_ = static_cast<State>(state);
}

}
