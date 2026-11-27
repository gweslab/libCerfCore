#include "avago_qpds_t900.h"

#include "../../core/board_parts.h"
#include "avago_qpds_t900_part_id.h"
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>

#include <algorithm>
#include <utility>

namespace cerfcore {

namespace {

constexpr uint8_t kAddr = 0x39u;

constexpr uint8_t kRegEnable  = 0x00u;
constexpr uint8_t kRegAtime   = 0x01u;
constexpr uint8_t kRegPtime   = 0x02u;
constexpr uint8_t kRegWtime   = 0x03u;
constexpr uint8_t kRegAiltl   = 0x04u;
constexpr uint8_t kRegAihtl   = 0x06u;
constexpr uint8_t kRegPiltl   = 0x08u;
constexpr uint8_t kRegPihtl   = 0x0Au;
constexpr uint8_t kRegPers    = 0x0Cu;
constexpr uint8_t kRegConfig  = 0x0Du;
constexpr uint8_t kRegPpcount = 0x0Eu;
constexpr uint8_t kRegControl = 0x0Fu;
constexpr uint8_t kRegRev     = 0x11u;
constexpr uint8_t kRegId      = 0x12u;
constexpr uint8_t kRegStatus  = 0x13u;
constexpr uint8_t kRegCdatal  = 0x14u;
constexpr uint8_t kRegPdatah  = 0x19u;

constexpr uint32_t kCh0Word  = 0u;
constexpr uint32_t kCh1Word  = 1u;
constexpr uint32_t kProxWord = 2u;

constexpr uint8_t kCmd           = 0x80u;
constexpr uint8_t kTypeMask      = 0x60u;
constexpr uint8_t kTypeShift     = 5u;
constexpr uint8_t kAddMask       = 0x1Fu;
constexpr uint8_t kTypeRepeated  = 0u;
constexpr uint8_t kTypeIncrement = 1u;
constexpr uint8_t kTypeSpecial   = 3u;

constexpr uint8_t kSpecialNone      = 0u;
constexpr uint8_t kSpecialClearProx = 5u;
constexpr uint8_t kSpecialClearAls  = 6u;
constexpr uint8_t kSpecialClearBoth = 7u;

constexpr uint8_t kPon        = 1u << 0;
constexpr uint8_t kAen        = 1u << 1;
constexpr uint8_t kPen        = 1u << 2;
constexpr uint8_t kWen        = 1u << 3;
constexpr uint8_t kAien       = 1u << 4;
constexpr uint8_t kPien       = 1u << 5;
constexpr uint8_t kEnableBit7 = 1u << 7;

constexpr uint8_t kAvalid = 1u << 0;
constexpr uint8_t kPvalid = 1u << 1;
constexpr uint8_t kAint   = 1u << 4;
constexpr uint8_t kPint   = 1u << 5;

constexpr uint8_t kWlong          = 1u << 1;
constexpr uint8_t kConfigReserved = static_cast<uint8_t>(~kWlong);

constexpr uint8_t kPdiodeMask = 0x30u;
constexpr uint8_t kPdiodeCh1  = 0x20u;
constexpr uint8_t kPgainMask  = 0x0Cu;
constexpr uint8_t kPgain1x    = 0x00u;

constexpr uint8_t kPtimeReset = 0xFFu;
constexpr uint8_t kWtimeReset = 0xFFu;
constexpr uint8_t kRevision   = 0x01u;
constexpr uint8_t kDeviceId   = 0x29u;

constexpr uint64_t kPulseHz       = 62500u;
constexpr uint64_t kStepTicks     = 170u;
constexpr uint64_t kStepsPerValue = 256u;
constexpr uint64_t kWlongFactor   = 12u;
constexpr uint64_t kStartupTicks  = kStepTicks;
constexpr uint64_t kAlsDelayTicks = kStepTicks;

constexpr uint32_t kPersMask       = 0x0Fu;
constexpr uint32_t kApersDirect    = 3u;
constexpr uint32_t kApersStep      = 5u;
constexpr uint32_t kMaxPersistence = kApersStep * (kPersMask - kApersDirect);
constexpr uint64_t kOutCountCap    = UINT8_MAX;

constexpr uint16_t kSceneCh0Counts  = 0u;
constexpr uint16_t kSceneCh1Counts  = 0u;
constexpr uint16_t kSceneProxCounts = 0u;

constexpr uint64_t kNever = UINT64_MAX;

uint64_t Steps(uint8_t value) { return kStepsPerValue - value; }

uint8_t CountOut(uint8_t count, uint64_t completions, bool out) {
    return out ? static_cast<uint8_t>(std::min<uint64_t>(count + completions, kOutCountCap)) : 0u;
}

}

bool AvagoQpdsT900::ShouldRegister() {
    return emu_.Get<BoardParts>().Has(PartId::AvagoQpdsT900);
}

void AvagoQpdsT900::OnReady() {
    clock_ = &emu_.Get<GuestCycleClock>();
    ticks_.Attach(kPulseHz, 1u);
    event_ = clock_->Add([this] {
        Advance();
        Schedule();
    });
    PowerOn();
    clock_->RegisterRateListener([this] {
        ticks_.Rescale();
        Advance();
        Schedule();
    });
    emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
        DriveInt();
        Advance();
        Schedule();
    });
}

bool AvagoQpdsT900::Timed(Phase phase) { return phase != Phase::kSleep && phase != Phase::kStart; }

void AvagoQpdsT900::PowerOn() {
    regs_.fill(0u);
    regs_[kRegPtime] = kPtimeReset;
    regs_[kRegWtime] = kWtimeReset;
    shadow_.fill(0u);
    address_         = kRegEnable;
    type_            = kTypeRepeated;
    command_pending_ = false;
    m_               = Machine{};
    clock_->Disarm(event_);
}

bool AvagoQpdsT900::MatchesAddress(uint8_t slave_addr) const { return slave_addr == kAddr; }

void AvagoQpdsT900::TxnStart(uint8_t) { command_pending_ = true; }

bool AvagoQpdsT900::TxnWriteByte(uint8_t, uint8_t byte) {
    if (command_pending_) {
        command_pending_ = false;
        if ((byte & kCmd) != 0u) {
            WriteCommand(byte);
            return true;
        }
    }
    WriteRegister(address_, byte);
    NextAddress();
    return true;
}

uint8_t AvagoQpdsT900::TxnReadByte(uint8_t) {
    command_pending_    = false;
    const uint8_t value = ReadRegister(address_);
    NextAddress();
    return value;
}

void AvagoQpdsT900::WriteCommand(uint8_t byte) {
    const uint8_t type = static_cast<uint8_t>((byte & kTypeMask) >> kTypeShift);
    if (type != kTypeRepeated && type != kTypeIncrement && type != kTypeSpecial) {
        emu_.Get<Fatal>().Die("avago qpds-t900: command byte 0x%02X is not modeled", byte);
    }
    if (type == kTypeSpecial) {
        SpecialFunction(static_cast<uint8_t>(byte & kAddMask));
        return;
    }
    type_    = type;
    address_ = static_cast<uint8_t>(byte & kAddMask);
}

void AvagoQpdsT900::SpecialFunction(uint8_t function) {
    Advance();
    switch (function) {
    case kSpecialNone:                                          break;
    case kSpecialClearProx: m_.pint = false;                    break;
    case kSpecialClearAls:  m_.aint = false;                    break;
    case kSpecialClearBoth: m_.pint = false; m_.aint = false;   break;
    default:
        emu_.Get<Fatal>().Die("avago qpds-t900: special function %u is not modeled", function);
    }
    DriveInt();
    Schedule();
}

void AvagoQpdsT900::NextAddress() {
    if (type_ == kTypeIncrement) address_ = static_cast<uint8_t>((address_ + 1u) & kAddMask);
}

void AvagoQpdsT900::WriteRegister(uint8_t reg, uint8_t value) {
    if (reg >= kControlRegisters) {
        emu_.Get<Fatal>().Die("avago qpds-t900: write of 0x%02X to register 0x%02X", value, reg);
    }
    if ((reg == kRegEnable && (value & kEnableBit7) != 0u) ||
        (reg == kRegConfig && (value & kConfigReserved) != 0u) ||
        (reg == kRegControl &&
         ((value & kPdiodeMask) != kPdiodeCh1 || (value & kPgainMask) != kPgain1x))) {
        emu_.Get<Fatal>().Die("avago qpds-t900: register 0x%02X write 0x%02X sets a reserved field",
                              reg, value);
    }
    Advance();
    regs_[reg] = value;
    if (reg == kRegEnable) {
        const uint64_t now = ticks_.Now();
        if (m_.phase == Phase::kSleep && (value & kPon) != 0u) {
            m_.phase     = Phase::kStartup;
            m_.phase_end = now + kStartupTicks;
        } else if (m_.phase == Phase::kStart) {
            CycleStart(m_, now, now);
        }
    }
    Schedule();
}

uint8_t AvagoQpdsT900::ReadRegister(uint8_t reg) {
    Advance();
    if (reg < kControlRegisters) return regs_[reg];
    switch (reg) {
    case kRegRev:    return kRevision;
    case kRegId:     return kDeviceId;
    case kRegStatus:
        return static_cast<uint8_t>(m_.valid | (m_.aint ? kAint : 0u) | (m_.pint ? kPint : 0u));
    default: break;
    }
    if (reg >= kRegCdatal && reg <= kRegPdatah) return ReadData(reg);
    emu_.Get<Fatal>().Die("avago qpds-t900: read of register 0x%02X", reg);
}

uint8_t AvagoQpdsT900::ReadData(uint8_t reg) {
    const uint32_t offset = static_cast<uint32_t>(reg - kRegCdatal);
    const uint32_t word   = offset >> 1;
    if ((offset & 1u) != 0u) return shadow_[word];
    shadow_[word] = static_cast<uint8_t>(m_.data[word] >> 8);
    return static_cast<uint8_t>(m_.data[word]);
}

uint16_t AvagoQpdsT900::RegWord(uint8_t low) const {
    return static_cast<uint16_t>(regs_[low] | (regs_[low + 1u] << 8));
}

uint64_t AvagoQpdsT900::ProxSpan() const {
    return regs_[kRegPpcount] + Steps(regs_[kRegPtime]) * kStepTicks;
}

uint64_t AvagoQpdsT900::WaitSpan() const {
    const uint64_t factor = (regs_[kRegConfig] & kWlong) != 0u ? kWlongFactor : 1u;
    return Steps(regs_[kRegWtime]) * kStepTicks * factor;
}

uint64_t AvagoQpdsT900::AlsSpan() const { return Steps(regs_[kRegAtime]) * kStepTicks; }

uint64_t AvagoQpdsT900::CycleSpan() const {
    const uint8_t enable = regs_[kRegEnable];
    return ((enable & kPen) != 0u ? ProxSpan() : 0u) + ((enable & kWen) != 0u ? WaitSpan() : 0u) +
           ((enable & kAen) != 0u ? kAlsDelayTicks + AlsSpan() : 0u);
}

uint32_t AvagoQpdsT900::ProxPersistence() const { return regs_[kRegPers] >> 4; }

uint32_t AvagoQpdsT900::AlsPersistence() const {
    const uint32_t apers = regs_[kRegPers] & kPersMask;
    return apers <= kApersDirect ? apers : kApersStep * (apers - kApersDirect);
}

void AvagoQpdsT900::Run(Machine& m, uint64_t now) const {
    while (Timed(m.phase) && m.phase_end <= now) {
        const uint64_t end = m.phase_end;
        switch (m.phase) {
        case Phase::kStartup: CycleStart(m, end, now); break;
        case Phase::kProx:
            ProxDone(m, 1u);
            WaitCheck(m, end, now);
            break;
        case Phase::kWait: AlsCheck(m, end, now); break;
        case Phase::kAlsDelay:
            m.phase     = Phase::kAls;
            m.phase_end = end + AlsSpan();
            break;
        case Phase::kAls:
            AlsDone(m, 1u);
            CycleStart(m, end, now);
            break;
        case Phase::kSleep:
        case Phase::kStart: break;
        }
    }
}

void AvagoQpdsT900::CycleStart(Machine& m, uint64_t at, uint64_t now) const {
    const uint8_t enable = regs_[kRegEnable];
    if ((enable & kPon) == 0u) {
        m.phase = Phase::kSleep;
        return;
    }
    if ((enable & (kPen | kWen | kAen)) == 0u) {
        m.phase = Phase::kStart;
        return;
    }
    const uint64_t span   = CycleSpan();
    const uint64_t cycles = now > at ? (now - at) / span : 0u;
    if (cycles != 0u) {
        if ((enable & kPen) != 0u) ProxDone(m, cycles);
        if ((enable & kAen) != 0u) AlsDone(m, cycles);
        at += cycles * span;
    }
    if ((enable & kPen) != 0u) {
        m.phase     = Phase::kProx;
        m.phase_end = at + ProxSpan();
        return;
    }
    WaitCheck(m, at, now);
}

void AvagoQpdsT900::WaitCheck(Machine& m, uint64_t at, uint64_t now) const {
    if ((regs_[kRegEnable] & kWen) != 0u) {
        m.phase     = Phase::kWait;
        m.phase_end = at + WaitSpan();
        return;
    }
    AlsCheck(m, at, now);
}

void AvagoQpdsT900::AlsCheck(Machine& m, uint64_t at, uint64_t now) const {
    if ((regs_[kRegEnable] & kAen) != 0u) {
        m.phase     = Phase::kAlsDelay;
        m.phase_end = at + kAlsDelayTicks;
        return;
    }
    CycleStart(m, at, now);
}

void AvagoQpdsT900::ProxDone(Machine& m, uint64_t count) const {
    m.data[kProxWord] = kSceneProxCounts;
    m.valid |= kPvalid;
    const bool out = m.data[kProxWord] < RegWord(kRegPiltl) || m.data[kProxWord] > RegWord(kRegPihtl);
    m.prox_out = CountOut(m.prox_out, count, out);
    const uint32_t persistence = ProxPersistence();
    if ((regs_[kRegEnable] & kPien) != 0u && (persistence == 0u || m.prox_out >= persistence)) {
        m.pint = true;
    }
}

void AvagoQpdsT900::AlsDone(Machine& m, uint64_t count) const {
    m.data[kCh0Word] = kSceneCh0Counts;
    m.data[kCh1Word] = kSceneCh1Counts;
    m.valid |= kAvalid;
    const bool out = m.data[kCh0Word] < RegWord(kRegAiltl) || m.data[kCh0Word] > RegWord(kRegAihtl);
    m.als_out = CountOut(m.als_out, count, out);
    const uint32_t persistence = AlsPersistence();
    if ((regs_[kRegEnable] & kAien) != 0u && (persistence == 0u || m.als_out >= persistence)) {
        m.aint = true;
    }
}

uint64_t AvagoQpdsT900::NextRaise(uint64_t now) const {
    const auto raised = [this](uint64_t until) {
        Machine sim = m_;
        Run(sim, until);
        return (sim.pint && !m_.pint) || (sim.aint && !m_.aint);
    };
    const uint64_t from    = Timed(m_.phase) ? std::max(now, m_.phase_end) : now;
    const uint64_t horizon = from + kStartupTicks + (kMaxPersistence + 2u) * CycleSpan();
    if (!raised(horizon)) return kNever;
    uint64_t low  = now;
    uint64_t high = horizon;
    while (high - low > 1u) {
        const uint64_t mid = low + (high - low) / 2u;
        if (raised(mid)) high = mid;
        else low = mid;
    }
    return high;
}

void AvagoQpdsT900::Advance() {
    const bool asserted = m_.pint || m_.aint;
    Run(m_, ticks_.Now());
    if ((m_.pint || m_.aint) != asserted) DriveInt();
}

void AvagoQpdsT900::Schedule() {
    const uint64_t raise = NextRaise(ticks_.Now());
    if (raise == kNever) {
        clock_->Disarm(event_);
        return;
    }
    ticks_.ArmAt(event_, raise);
}

void AvagoQpdsT900::DriveInt() { int_listeners_.Call(!(m_.pint || m_.aint)); }

void AvagoQpdsT900::AddIntListener(CallbackList<bool>::Callback listener) {
    listener(!(m_.pint || m_.aint));
    int_listeners_.Add(std::move(listener));
}

void AvagoQpdsT900::SaveState(StateWriter& w) {
    ticks_.Save(w, "qpds_");
    for (uint8_t reg : regs_) w.Write<uint8_t>("qpds_reg", reg);
    for (uint8_t high : shadow_) w.Write<uint8_t>("qpds_shadow", high);
    w.Write<uint8_t>("qpds_address", address_);
    w.Write<uint8_t>("qpds_type", type_);
    w.Write<uint8_t>("qpds_command_pending", command_pending_ ? 1u : 0u);
    w.Write<uint8_t>("qpds_phase", static_cast<uint8_t>(m_.phase));
    w.Write<uint64_t>("qpds_phase_end", m_.phase_end);
    w.Write<uint8_t>("qpds_valid", m_.valid);
    w.Write<uint8_t>("qpds_prox_out", m_.prox_out);
    w.Write<uint8_t>("qpds_als_out", m_.als_out);
    w.Write<uint8_t>("qpds_pint", m_.pint ? 1u : 0u);
    w.Write<uint8_t>("qpds_aint", m_.aint ? 1u : 0u);
    for (uint16_t word : m_.data) w.Write<uint16_t>("qpds_data", word);
}

void AvagoQpdsT900::RestoreState(StateReader& r) {
    ticks_.Restore(r, "qpds_");
    for (uint8_t& reg : regs_) r.Read("qpds_reg", reg);
    for (uint8_t& high : shadow_) r.Read("qpds_shadow", high);
    uint8_t pending = 0u, phase = 0u, pint = 0u, aint = 0u;
    r.Read("qpds_address", address_);
    r.Read("qpds_type", type_);
    r.Read("qpds_command_pending", pending);
    r.Read("qpds_phase", phase);
    r.Read("qpds_phase_end", m_.phase_end);
    r.Read("qpds_valid", m_.valid);
    r.Read("qpds_prox_out", m_.prox_out);
    r.Read("qpds_als_out", m_.als_out);
    r.Read("qpds_pint", pint);
    r.Read("qpds_aint", aint);
    for (uint16_t& word : m_.data) r.Read("qpds_data", word);
    command_pending_ = pending != 0u;
    m_.phase         = static_cast<Phase>(phase);
    m_.pint          = pint != 0u;
    m_.aint          = aint != 0u;
}

void AvagoQpdsT900::PostRestore() {
    DriveInt();
    Advance();
    Schedule();
}

REGISTER_SERVICE(AvagoQpdsT900);

}
