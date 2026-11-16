#include "cortex_m3_core.h"

namespace cerfcore {

namespace cortex_m3 {

namespace {

constexpr uint32_t kScsBase = 0xE000E000u;
constexpr uint32_t kScsEnd = 0xE000F000u;

constexpr uint32_t kSystCsr = 0xE000E010u;
constexpr uint32_t kSystRvr = 0xE000E014u;
constexpr uint32_t kSystCvr = 0xE000E018u;
constexpr uint32_t kIserBase = 0xE000E100u;
constexpr uint32_t kIcerBase = 0xE000E180u;
constexpr uint32_t kIsprBase = 0xE000E200u;
constexpr uint32_t kIcprBase = 0xE000E280u;
constexpr uint32_t kIabrBase = 0xE000E300u;
constexpr uint32_t kBankBytes = 0x40u;
constexpr uint32_t kIprBase = 0xE000E400u;
constexpr uint32_t kIprEnd = 0xE000E5F0u;
constexpr uint32_t kIcsr = 0xE000ED04u;
constexpr uint32_t kVtor = 0xE000ED08u;
constexpr uint32_t kAircr = 0xE000ED0Cu;
constexpr uint32_t kScr = 0xE000ED10u;
constexpr uint32_t kCcr = 0xE000ED14u;
constexpr uint32_t kShprBase = 0xE000ED18u;
constexpr uint32_t kShprEnd = 0xE000ED24u;
constexpr uint32_t kCfsr = 0xE000ED28u;
constexpr uint32_t kHfsr = 0xE000ED2Cu;
constexpr uint32_t kDfsr = 0xE000ED30u;
constexpr uint32_t kStir = 0xE000EF00u;

bool InBank(uint32_t address, uint32_t base) {
    return address >= base && address < base + kBankBytes;
}

}

bool Core::IsScs(uint32_t address) {
    return address >= kScsBase && address < kScsEnd;
}

uint32_t Core::ScsRead(Bus& bus, uint32_t address, uint32_t size) {
    CheckPpbPrivilege(bus, address, false);
    if ((address >= kIprBase && address < kIprEnd) || (address >= kShprBase && address < kShprEnd)) {
        const uint32_t first = address >= kIprBase ? kExcExternalBase + (address - kIprBase)
                                                   : 4u + (address - kShprBase);
        uint32_t value = 0u;
        for (uint32_t i = 0u; i < size; ++i) {
            if (first + i < ImplementedExceptions() && HasConfigurablePriority(first + i)) value |= static_cast<uint32_t>(state_.priority[first + i]) << (8u * i);
        }
        return value;
    }
    if ((address & ~3u) == kCfsr) {
        const uint32_t mask = size == 4u ? 0xFFFFFFFFu : (1u << (8u * size)) - 1u;
        return (state_.cfsr >> (8u * (address & 3u))) & mask;
    }
    if (size != 4u) Raise(bus, "Cortex-M3: sub-word access to a word-only SCS register", address);
    if (InBank(address, kIserBase) || InBank(address, kIcerBase) || InBank(address, kIsprBase) ||
        InBank(address, kIcprBase) || InBank(address, kIabrBase)) {
        const uint32_t bank = address & 0x3Fu;
        const uint32_t base = address - bank;
        uint32_t value = 0u;
        for (uint32_t i = 0u; i < 32u; ++i) {
            const uint32_t exception = kExcExternalBase + bank * 8u + i;
            if (exception >= ImplementedExceptions()) continue;
            const bool set = (base == kIserBase || base == kIcerBase) ? state_.enabled[exception] != 0u
                           : (base == kIabrBase)                      ? state_.active[exception] != 0u
                                                                      : IsPending(exception);
            if (set) value |= 1u << i;
        }
        return value;
    }
    switch (address) {
    case kSystCsr: {
        const uint32_t value = state_.syst_csr;
        state_.syst_csr &= ~(1u << 16);
        return value;
    }
    case kSystRvr: return state_.syst_rvr;
    case kSystCvr: return state_.syst_cvr;
    case kIcsr: {
        uint32_t value = state_.handler_mode ? (state_.ipsr & kIpsrMask) : 0u;
        const uint32_t current = state_.ipsr & kIpsrMask;
        const uint32_t others = ActiveCount() - (current < kMaxExceptions && state_.active[current] != 0u ? 1u : 0u);
        if (others == 0u) value |= 1u << 11;
        const uint32_t vectpending = HighestPending();
        if (vectpending != 0u && GroupPriority(vectpending) < BoostedPriority()) value |= vectpending << 12;
        for (uint32_t i = kExcExternalBase; i < kMaxExceptions; ++i) {
            if (!IsPending(i)) continue;
            value |= 1u << 22;
            break;
        }
        if (state_.pending[kExcSysTick] != 0u) value |= 1u << 26;
        if (state_.pending[kExcPendSv] != 0u) value |= 1u << 28;
        return value;
    }
    case kVtor: return state_.vtor;
    case kAircr: return 0xFA050000u | (state_.prigroup << 8);
    case kCcr: return state_.ccr;
    case kScr: return state_.scr;
    case kHfsr: return state_.hfsr;
    case kDfsr: return state_.dfsr;
    default: Raise(bus, "Cortex-M3: read of an unmodelled System Control Space register", address);
    }
}

void Core::CheckPpbPrivilege(Bus& bus, uint32_t address, bool write) {
    if ((Privileged() && !unprivileged_access_) || (write && address == kStir && (state_.ccr & kCcrUserSetMPend) != 0u)) return;
    Raise(bus, "Cortex-M3: BusFault on an unprivileged access to the System Control Space", address);
}

void Core::ScsWrite(Bus& bus, uint32_t address, uint32_t size, uint32_t value) {
    CheckPpbPrivilege(bus, address, true);
    MarkExceptionsDirty();
    if ((address >= kIprBase && address < kIprEnd) || (address >= kShprBase && address < kShprEnd)) {
        const uint32_t first = address >= kIprBase ? kExcExternalBase + (address - kIprBase)
                                                   : 4u + (address - kShprBase);
        for (uint32_t i = 0u; i < size; ++i) {
            if (first + i >= ImplementedExceptions()) {
                if (((value >> (8u * i)) & 0xFFu) != 0u)
                    Raise(bus, "Cortex-M3: priority write for an interrupt line the NVIC does not have", address);
                continue;
            }
            if (!HasConfigurablePriority(first + i)) continue;
            state_.priority[first + i] = static_cast<uint8_t>((value >> (8u * i)) & kPriorityMask);
        }
        return;
    }
    if ((address & ~3u) == kCfsr) {
        const uint32_t mask = size == 4u ? 0xFFFFFFFFu : (1u << (8u * size)) - 1u;
        state_.cfsr &= ~((value & mask) << (8u * (address & 3u)));
        return;
    }
    if (size != 4u) Raise(bus, "Cortex-M3: sub-word write to a word-only SCS register", address);
    if (InBank(address, kIserBase) || InBank(address, kIcerBase) || InBank(address, kIsprBase) ||
        InBank(address, kIcprBase)) {
        const uint32_t bank = address & 0x3Fu;
        const uint32_t base = address - bank;
        for (uint32_t i = 0u; i < 32u; ++i) {
            if ((value & (1u << i)) == 0u) continue;
            const uint32_t exception = kExcExternalBase + bank * 8u + i;
            if (exception >= ImplementedExceptions()) {
                if (base == kIsprBase) Raise(bus, "Cortex-M3: set-pending of an interrupt line the NVIC does not have", address);
                continue;
            }
            if (base == kIserBase) state_.enabled[exception] = 1u;
            if (base == kIcerBase) state_.enabled[exception] = 0u;
            if (base == kIsprBase) SetPending(exception);
            if (base == kIcprBase) state_.pending[exception] = 0u;
        }
        return;
    }
    switch (address) {
    case kSystCsr:
        state_.syst_csr = (state_.syst_csr & (1u << 16)) | (value & 7u);
        return;
    case kSystRvr: state_.syst_rvr = value & 0x00FFFFFFu; return;
    case kSystCvr:
        state_.syst_cvr = 0u;
        state_.syst_csr &= ~(1u << 16);
        return;
    case kIcsr:
        if ((value & (3u << 27)) == (3u << 27) || (value & (3u << 25)) == (3u << 25))
            Raise(bus, "Cortex-M3: UNPREDICTABLE ICSR write sets a pending bit and its clear bit", value);
        if ((value & (1u << 31)) != 0u) Raise(bus, "Cortex-M3: NMIPENDSET is not modelled", value);
        if ((value & (1u << 28)) != 0u) SetPending(kExcPendSv);
        if ((value & (1u << 27)) != 0u) state_.pending[kExcPendSv] = 0u;
        if ((value & (1u << 26)) != 0u) SetPending(kExcSysTick);
        if ((value & (1u << 25)) != 0u) state_.pending[kExcSysTick] = 0u;
        return;
    case kStir: {
        const uint32_t exception = kExcExternalBase + (value & 0x1FFu);
        if (exception >= ImplementedExceptions()) Raise(bus, "Cortex-M3: STIR write for an interrupt line the NVIC does not have", value);
        SetPending(exception);
        return;
    }
    case kVtor: state_.vtor = value & 0x3FFFFF80u; return;
    case kAircr:
        if ((value >> 16) != 0x05FAu) return;
        if ((value & 3u) != 0u) Raise(bus, "Cortex-M3: UNPREDICTABLE AIRCR write to VECTCLRACTIVE or VECTRESET", value);
        state_.prigroup = (value >> 8) & 7u;
        if ((value & 4u) != 0u) bus.SystemResetRequest();
        return;
    case kScr: state_.scr = value & (kScrSevOnPend | kScrSleepDeep | kScrSleepOnExit); return;
    case kHfsr: state_.hfsr &= ~value; return;
    case kDfsr: state_.dfsr &= ~value; return;
    case kCcr:
        if ((value & kCcrBfhfnmign) != 0u)
            Raise(bus, "Cortex-M3: CCR.BFHFNMIGN is not modelled, the bus has no recoverable precise BusFault", value);
        state_.ccr = value & 0x31Bu;
        return;
    default: Raise(bus, "Cortex-M3: write of an unmodelled System Control Space register", address);
    }
}

}

}
