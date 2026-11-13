#include "sh77xx_dmac.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <cerfengine/sh/sh_bus_master_coherence.h>
#include "../../cpu/physical_bus.h"
#include "../../execution/engine_bridge.h"
#include "../../peripherals/peripheral_dispatcher.h"
#include "sh77xx_intc.h"
#include "sh77xx_standby_entry.h"

#include <cstdint>
#include <string>

namespace cerfcore {

namespace {

constexpr uint32_t kChannelStride = 0x10u;
constexpr uint32_t kOffDmaor      = 0x40u;

constexpr uint32_t kChcrDe        = 0x00000001u;
constexpr uint32_t kChcrTe        = 0x00000002u;
constexpr uint32_t kChcrIe        = 0x00000004u;
constexpr uint32_t kChcrTm        = 0x00000020u;
constexpr uint32_t kChcrRo        = 0x00080000u;
constexpr uint32_t kChcrDi        = 0x00100000u;
constexpr uint32_t kChcrReserved  = 0xFFE00000u;
constexpr uint32_t kChcrCommon    = 0x0000FF3Fu;
constexpr uint32_t kChcrExtra[4]  = {0x00070040u, 0x00070040u, kChcrRo, kChcrDi};
constexpr uint32_t kChcrTsShift   = 3u;
constexpr uint32_t kChcrRsShift   = 8u;
constexpr uint32_t kChcrSmShift   = 12u;
constexpr uint32_t kChcrDmShift   = 14u;
constexpr uint32_t kFieldMask     = 3u;
constexpr uint32_t kIllegalField  = 3u;

constexpr uint32_t kRsExternal    = 0x0u;
constexpr uint32_t kRsAuto        = 0x4u;
constexpr uint32_t kRsModuleFirst = 0xAu;
constexpr uint32_t kRsIrdaTx      = 0xAu;
constexpr uint32_t kRsIrdaRx      = 0xBu;
constexpr uint32_t kRsScifTx      = 0xCu;
constexpr uint32_t kRsScifRx      = 0xDu;
constexpr uint32_t kRsAd          = 0xEu;
constexpr uint32_t kRsSerialLast  = kRsScifRx;

constexpr uint16_t kDmaorDme     = 0x0001u;
constexpr uint16_t kDmaorStored  = 0x0301u;
constexpr uint32_t kDmaorPrShift = 8u;
constexpr uint32_t kRoundRobin   = 3u;

constexpr uint8_t kFixedOrder[3][4] = {{0u, 1u, 2u, 3u}, {0u, 2u, 3u, 1u}, {2u, 0u, 1u, 3u}};

constexpr uint32_t kTcrMask      = 0x00FFFFFFu;
constexpr uint32_t kReloadPeriod = 4u;
constexpr uint32_t kPhysicalMask = 0x1FFFFFFFu;
constexpr uint32_t kAreaShift    = 29u;
constexpr uint32_t kNoShadow     = 7u;
constexpr uint32_t kArea1First   = 0x04000000u;
constexpr uint32_t kArea1End     = 0x08000000u;
constexpr uint32_t kArea7        = 0x1C000000u;

struct PaRange {
    uint32_t first;
    uint32_t end;
};

constexpr PaRange kAdRange        = {0x04000080u, 0x04000094u};
constexpr PaRange kAdDataRange    = {0x04000080u, 0x04000090u};
constexpr PaRange kDaRange        = {0x040000A0u, 0x040000A6u};
constexpr PaRange kPortRange      = {0x04000100u, 0x04000138u};
constexpr PaRange kIrdaRange      = {0x04000140u, 0x04000150u};
constexpr PaRange kScifRange      = {0x04000150u, 0x04000160u};
constexpr PaRange kDmaReachable[] = {kAdRange, kDaRange, kPortRange, kIrdaRange, kScifRange};

constexpr uint32_t kIrdaTdr = 0x04000146u;
constexpr uint32_t kIrdaRdr = 0x0400014Au;
constexpr uint32_t kScifTdr = 0x04000156u;
constexpr uint32_t kScifRdr = 0x0400015Au;

constexpr uint8_t kStaleSar = 0x1u;
constexpr uint8_t kStaleDar = 0x2u;
constexpr uint8_t kStaleTcr = 0x4u;
constexpr uint8_t kStaleAll = kStaleSar | kStaleDar | kStaleTcr;

constexpr uint8_t kStbcr2Mstp7 = 0x10u;

constexpr const char* kChannelPrefix[4] = {"ch0_", "ch1_", "ch2_", "ch3_"};

uint32_t ChcrField(uint32_t chcr, uint32_t shift) { return (chcr >> shift) & kFieldMask; }

uint32_t RequestSelect(uint32_t chcr) { return (chcr >> kChcrRsShift) & 0xFu; }

uint32_t ModuleRs(Sh77xxDmaRequest source) { return kRsModuleFirst + static_cast<uint32_t>(source); }

uint8_t ModuleBit(uint32_t rs) { return static_cast<uint8_t>(1u << (rs - kRsModuleFirst)); }

uint32_t Step(uint32_t mode, uint32_t bytes) {
    if (mode == 1u) return bytes;
    if (mode == 2u) return 0u - bytes;
    return 0u;
}

std::string Name(uint32_t channel, const char* field) { return std::string(kChannelPrefix[channel]) + field; }

bool Inside(const PaRange& range, uint32_t pa, uint32_t bytes) { return pa >= range.first && pa + bytes <= range.end; }

bool DmaReachable(uint32_t pa, uint32_t bytes) {
    for (const PaRange& range : kDmaReachable) {
        if (Inside(range, pa, bytes)) return true;
    }
    return false;
}

}

void Sh77xxDmac::OnReady() {
    intc_      = &emu_.Get<Sh77xxIntc>();
    stbcr2_    = &emu_.Get<Sh77xxStbcr2>();
    bus_       = &emu_.Get<PhysicalBus>();
    coherence_ = &emu_.Get<EngineBridge>().Engine().Get<cerfengine::ShBusMasterCoherence>();
    stbcr2_->RegisterChangeListener([this](uint8_t from, uint8_t to) {
        if ((from & kStbcr2Mstp7) != 0u && (to & kStbcr2Mstp7) == 0u) Service();
    });
    emu_.Get<Sh77xxStandbyEntry>().RegisterListener([this] { Initialize(); });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        Initialize();
        rr_first_ = 0u;
    });
    Initialize();
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t Sh77xxDmac::Index(const Channel& ch) const { return static_cast<uint32_t>(&ch - ch_); }

Sh77xxDmac::Channel& Sh77xxDmac::Locate(uint32_t addr, Field& field) {
    const uint32_t off = addr - MmioBase();
    field              = static_cast<Field>((off % kChannelStride) / 4u);
    return ch_[off / kChannelStride];
}

uint32_t Sh77xxDmac::ReadWord(uint32_t addr) {
    const uint32_t off = addr - MmioBase();
    if (off >= kOffDmaor || (off & 3u) != 0u) HaltUnsupportedAccess("ReadWord", addr, 0u);
    Field    field = Field::kSar;
    Channel& ch    = Locate(addr, field);
    return ReadChannel(ch, field);
}

void Sh77xxDmac::WriteWord(uint32_t addr, uint32_t value) {
    const uint32_t off = addr - MmioBase();
    if (off >= kOffDmaor || (off & 3u) != 0u) HaltUnsupportedAccess("WriteWord", addr, value);
    Field    field = Field::kSar;
    Channel& ch    = Locate(addr, field);
    WriteChannel(ch, field, value);
}

uint16_t Sh77xxDmac::ReadHalf(uint32_t addr) {
    if (addr - MmioBase() != kOffDmaor) HaltUnsupportedAccess("ReadHalf", addr, 0u);
    return dmaor_;
}

void Sh77xxDmac::WriteHalf(uint32_t addr, uint16_t value) {
    if (addr - MmioBase() != kOffDmaor) HaltUnsupportedAccess("WriteHalf", addr, value);
    WriteDmaor(value);
}

uint32_t Sh77xxDmac::ReadChannel(Channel& ch, Field field) {
    switch (field) {
        case Field::kSar: return ch.sar;
        case Field::kDar: return ch.dar;
        case Field::kTcr: return ch.tcr;
        case Field::kChcr: break;
    }
    if ((ch.chcr & kChcrTe) != 0u) ch.te_read = true;
    return ch.chcr;
}

void Sh77xxDmac::WriteChannel(Channel& ch, Field field, uint32_t value) {
    if (field != Field::kChcr && Armed(ch)) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u register write 0x%08X at offset 0x%X while DE and DME are set",
                              Index(ch), value, static_cast<uint32_t>(field) * 4u);
    }
    switch (field) {
        case Field::kSar:
            ch.sar         = value;
            ch.reload_base = value;
            ch.stale       = static_cast<uint8_t>(ch.stale & ~kStaleSar);
            return;
        case Field::kDar:
            ch.dar   = value;
            ch.stale = static_cast<uint8_t>(ch.stale & ~kStaleDar);
            return;
        case Field::kTcr:
            ch.tcr       = value & kTcrMask;
            ch.tcr_armed = true;
            ch.stale     = static_cast<uint8_t>(ch.stale & ~kStaleTcr);
            return;
        case Field::kChcr:
            WriteChcr(ch, value);
            return;
    }
}

void Sh77xxDmac::WriteChcr(Channel& ch, uint32_t value) {
    const uint32_t i = Index(ch);
    if ((value & kChcrReserved) != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: CHCR%u write 0x%08X sets reserved bits 31-21", i, value);
    }
    const bool enabled = (ch.chcr & kChcrDe) != 0u;
    if (enabled && RequestSelect(value) != RequestSelect(ch.chcr)) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: CHCR%u write 0x%08X changes RS while DE is 1", i, value);
    }
    const bool armed = Armed(ch);
    const bool te    = (ch.chcr & kChcrTe) != 0u && ((value & kChcrTe) != 0u || !ch.te_read);
    ch.chcr          = (value & (kChcrCommon | kChcrExtra[i]) & ~kChcrTe) | (te ? kChcrTe : 0u);
    if (!te) ch.te_read = false;
    if (enabled && (ch.chcr & kChcrDe) == 0u) AbortReload(ch);
    if (Armed(ch)) {
        ValidateFields(ch);
        if (!armed) ValidateStart(ch);
    }
    Drive(ch);
    Service();
}

void Sh77xxDmac::WriteDmaor(uint16_t value) {
    bool armed[kChannels];
    for (const Channel& ch : ch_) armed[Index(ch)] = Armed(ch);
    const bool enabled = (dmaor_ & kDmaorDme) != 0u;
    dmaor_             = static_cast<uint16_t>(value & kDmaorStored);
    if (enabled && (dmaor_ & kDmaorDme) == 0u) {
        for (Channel& ch : ch_) AbortReload(ch);
    }
    for (const Channel& ch : ch_) {
        if (armed[Index(ch)] || !Armed(ch)) continue;
        ValidateFields(ch);
        ValidateStart(ch);
    }
    Service();
}

void Sh77xxDmac::AbortReload(Channel& ch) {
    ch.reload_count = 0u;
    ch.stale        = kStaleAll;
}

bool Sh77xxDmac::ClockStopped() const { return stbcr2_->ModuleStopped(kStbcr2Mstp7); }

bool Sh77xxDmac::Armed(const Channel& ch) const {
    return (ch.chcr & (kChcrDe | kChcrTe)) == kChcrDe && (dmaor_ & kDmaorDme) != 0u;
}

bool Sh77xxDmac::Enabled(const Channel& ch) const { return Armed(ch) && !ClockStopped(); }

bool Sh77xxDmac::Requested(const Channel& ch) const {
    const uint32_t rs = RequestSelect(ch.chcr);
    if (rs == kRsAuto) return true;
    return (pending_ & ModuleBit(rs)) != 0u;
}

void Sh77xxDmac::ValidateFields(const Channel& ch) {
    const uint32_t i  = Index(ch);
    const uint32_t rs = RequestSelect(ch.chcr);
    Fatal&         fatal = emu_.Get<Fatal>();
    if (rs == kRsExternal && i < 2u) {
        fatal.Die("Sh77xxDmac: channel %u enabled for an external DREQ%u request, which is not modeled", i, i);
    }
    if (rs != kRsAuto && rs < kRsModuleFirst) {
        fatal.Die("Sh77xxDmac: channel %u enabled with the illegal RS setting 0x%X", i, rs);
    }
    if (ChcrField(ch.chcr, kChcrTsShift) == kIllegalField || ChcrField(ch.chcr, kChcrSmShift) == kIllegalField ||
        ChcrField(ch.chcr, kChcrDmShift) == kIllegalField) {
        fatal.Die("Sh77xxDmac: channel %u enabled with the illegal CHCR 0x%08X", i, ch.chcr);
    }
    if (rs >= kRsModuleFirst && rs <= kRsSerialLast && (ch.chcr & kChcrTm) != 0u) {
        fatal.Die("Sh77xxDmac: channel %u enabled in burst mode for the serial request source 0x%X", i, rs);
    }
    if ((ch.chcr & kChcrRo) != 0u && (ch.chcr & kChcrTm) == 0u) {
        fatal.Die("Sh77xxDmac: channel %u reloads SAR in cycle-steal mode", i);
    }
}

void Sh77xxDmac::ValidateStart(const Channel& ch) {
    const uint32_t i     = Index(ch);
    Fatal&         fatal = emu_.Get<Fatal>();
    if (!ch.tcr_armed) {
        fatal.Die("Sh77xxDmac: channel %u enabled without a DMATCR write since its last normal end", i);
    }
    if ((ch.chcr & kChcrRo) != 0u) {
        if (ch.tcr != kReloadPeriod) {
            fatal.Die("Sh77xxDmac: channel %u reloads SAR in burst mode with DMATCR 0x%06X, not 4", i, ch.tcr);
        }
        if (ch.stale != 0u) {
            fatal.Die("Sh77xxDmac: channel %u restarts the SAR reload after a DE or DME clear without rewriting "
                      "SAR, DAR and DMATCR",
                      i);
        }
    }
    const uint32_t rs = RequestSelect(ch.chcr);
    ValidateRequestRegister(ch, rs);
    if (rs >= kRsModuleFirst && rs <= kRsSerialLast) {
        fatal.Die("Sh77xxDmac: channel %u enabled for the serial request source 0x%X, which is not modeled", i, rs);
    }
}

void Sh77xxDmac::ValidateRequestRegister(const Channel& ch, uint32_t rs) {
    const uint32_t sar = ch.sar & kPhysicalMask;
    const uint32_t dar = ch.dar & kPhysicalMask;
    bool           ok  = true;
    switch (rs) {
        case kRsIrdaTx: ok = dar == kIrdaTdr; break;
        case kRsIrdaRx: ok = sar == kIrdaRdr; break;
        case kRsScifTx: ok = dar == kScifTdr; break;
        case kRsScifRx: ok = sar == kScifRdr; break;
        case kRsAd: ok = Inside(kAdDataRange, sar, 1u); break;
        default: break;
    }
    if (!ok) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u requested by RS 0x%X with SAR 0x%08X and DAR 0x%08X off the data "
                              "register of the requesting module",
                              Index(ch), rs, ch.sar, ch.dar);
    }
}

int Sh77xxDmac::Pick() {
    const uint32_t pr = (dmaor_ >> kDmaorPrShift) & kFieldMask;
    for (uint32_t k = 0u; k < kChannels; ++k) {
        const uint32_t i  = pr == kRoundRobin ? (rr_first_ + k) % kChannels : kFixedOrder[pr][k];
        const Channel& ch = ch_[i];
        if (Enabled(ch) && Requested(ch)) return static_cast<int>(i);
    }
    return -1;
}

bool Sh77xxDmac::TakesRequest(Sh77xxDmaRequest source) const {
    const uint32_t rs = ModuleRs(source);
    for (const Channel& ch : ch_) {
        if (Enabled(ch) && RequestSelect(ch.chcr) == rs) return true;
    }
    return false;
}

bool Sh77xxDmac::Transferring(Sh77xxDmaRequest source) const {
    return transferring_rs_ == ModuleRs(source);
}

void Sh77xxDmac::Request(Sh77xxDmaRequest source) {
    pending_ = static_cast<uint8_t>(pending_ | ModuleBit(ModuleRs(source)));
    Service();
}

void Sh77xxDmac::Withdraw(Sh77xxDmaRequest source) { ClearModuleRequest(ModuleRs(source)); }

void Sh77xxDmac::ClearModuleRequest(uint32_t rs) { pending_ = static_cast<uint8_t>(pending_ & ~ModuleBit(rs)); }

void Sh77xxDmac::Service() {
    if (servicing_) return;
    servicing_ = true;
    for (int i = Pick(); i >= 0; i = Pick()) TransferUnit(ch_[i]);
    servicing_ = false;
}

void Sh77xxDmac::TransferUnit(Channel& ch) {
    const uint32_t i        = Index(ch);
    const uint32_t bytes    = 1u << ChcrField(ch.chcr, kChcrTsShift);
    const bool     indirect = (ch.chcr & kChcrDi) != 0u;
    const uint32_t rs       = RequestSelect(ch.chcr);
    transferring_rs_        = static_cast<uint8_t>(rs);
    const uint32_t source   = indirect ? BusRead(ch, ch.sar, 4u) : ch.sar;
    BusWrite(ch, ch.dar, bytes, BusRead(ch, source, bytes));
    transferring_rs_ = 0u;
    ch.sar += Step(ChcrField(ch.chcr, kChcrSmShift), indirect ? 4u : bytes);
    ch.dar += Step(ChcrField(ch.chcr, kChcrDmShift), bytes);
    if ((ch.chcr & kChcrRo) != 0u && ++ch.reload_count == kReloadPeriod) {
        ch.sar          = ch.reload_base;
        ch.reload_count = 0u;
    }
    ch.tcr            = (ch.tcr - 1u) & kTcrMask;
    const bool done = ch.tcr == 0u;
    if (rs >= kRsModuleFirst && (done || (ch.chcr & kChcrTm) == 0u)) ClearModuleRequest(rs);
    if (((dmaor_ >> kDmaorPrShift) & kFieldMask) == kRoundRobin) {
        rr_first_ = static_cast<uint8_t>((i + 1u) % kChannels);
    }
    if (!done) return;
    ch.chcr |= kChcrTe;
    ch.te_read      = false;
    ch.tcr_armed    = false;
    ch.reload_count = 0u;
    Drive(ch);
}

uint32_t Sh77xxDmac::BusAddress(const Channel& ch, uint32_t addr, uint32_t bytes, const char* side) {
    const uint32_t i = Index(ch);
    if (addr % bytes != 0u) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u %s address 0x%08X is not aligned to %u bytes", i, side, addr,
                              bytes);
    }
    const uint32_t pa = addr & kPhysicalMask;
    if (pa >= kArea7) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u %s address 0x%08X is in reserved area 7", i, side, addr);
    }
    if ((addr >> kAreaShift) == kNoShadow) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u %s address 0x%08X is in no area of the physical map", i, side,
                              addr);
    }
    if (pa >= kArea1First && pa < kArea1End && !DmaReachable(pa, bytes)) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u %s address 0x%08X is an on-chip register the DMAC may not access",
                              i, side, addr);
    }
    return pa;
}

uint32_t Sh77xxDmac::BusRead(const Channel& ch, uint32_t addr, uint32_t bytes) {
    const uint32_t pa    = BusAddress(ch, addr, bytes, "read");
    uint32_t       value = 0u;
    if (!bus_->Read(pa, static_cast<MmioWidth>(bytes), &value)) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u reads %u bytes at unmapped PA 0x%08X", Index(ch), bytes, pa);
    }
    return value;
}

void Sh77xxDmac::BusWrite(const Channel& ch, uint32_t addr, uint32_t bytes, uint32_t value) {
    const uint32_t pa = BusAddress(ch, addr, bytes, "write");
    if (!bus_->Write(pa, static_cast<MmioWidth>(bytes), value)) {
        emu_.Get<Fatal>().Die("Sh77xxDmac: channel %u writes %u bytes of 0x%08X at unmapped PA 0x%08X", Index(ch),
                              bytes, value, pa);
    }
    coherence_->AfterWrite(pa, bytes);
}

void Sh77xxDmac::Drive(const Channel& ch) {
    const auto source = static_cast<Sh77xxIntcSource>(static_cast<uint32_t>(Sh77xxIntcSource::kDei0) + Index(ch));
    intc_->SetSourceRequest(source, (ch.chcr & (kChcrTe | kChcrIe)) == (kChcrTe | kChcrIe));
}

void Sh77xxDmac::Initialize() {
    for (Channel& ch : ch_) {
        ch.chcr         = 0u;
        ch.reload_count = 0u;
        ch.stale        = kStaleAll;
        ch.tcr_armed    = true;
        ch.te_read      = false;
        Drive(ch);
    }
    dmaor_   = 0u;
    pending_ = 0u;
}

void Sh77xxDmac::SaveState(StateWriter& w) {
    for (const Channel& ch : ch_) {
        const uint32_t i = Index(ch);
        w.Write(Name(i, "sar").c_str(), ch.sar);
        w.Write(Name(i, "dar").c_str(), ch.dar);
        w.Write(Name(i, "tcr").c_str(), ch.tcr);
        w.Write(Name(i, "chcr").c_str(), ch.chcr);
        w.Write(Name(i, "reload_base").c_str(), ch.reload_base);
        w.Write(Name(i, "reload_count").c_str(), ch.reload_count);
        w.Write(Name(i, "stale").c_str(), ch.stale);
        w.Write(Name(i, "tcr_armed").c_str(), ch.tcr_armed);
        w.Write(Name(i, "te_read").c_str(), ch.te_read);
    }
    w.Write("dmaor", dmaor_);
    w.Write("pending", pending_);
    w.Write("rr_first", rr_first_);
}

void Sh77xxDmac::RestoreState(StateReader& r) {
    for (Channel& ch : ch_) {
        const uint32_t i = Index(ch);
        r.Read(Name(i, "sar").c_str(), ch.sar);
        r.Read(Name(i, "dar").c_str(), ch.dar);
        r.Read(Name(i, "tcr").c_str(), ch.tcr);
        r.Read(Name(i, "chcr").c_str(), ch.chcr);
        r.Read(Name(i, "reload_base").c_str(), ch.reload_base);
        r.Read(Name(i, "reload_count").c_str(), ch.reload_count);
        r.Read(Name(i, "stale").c_str(), ch.stale);
        r.Read(Name(i, "tcr_armed").c_str(), ch.tcr_armed);
        r.Read(Name(i, "te_read").c_str(), ch.te_read);
    }
    r.Read("dmaor", dmaor_);
    r.Read("pending", pending_);
    r.Read("rr_first", rr_first_);
}

void Sh77xxDmac::PostRestore() {
    for (const Channel& ch : ch_) Drive(ch);
}

}
