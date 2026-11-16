#pragma once

#include <array>
#include <cstdint>

namespace cerfcore {

namespace cortex_m3 {

inline constexpr uint32_t kPsrN = 1u << 31;
inline constexpr uint32_t kPsrZ = 1u << 30;
inline constexpr uint32_t kPsrC = 1u << 29;
inline constexpr uint32_t kPsrV = 1u << 28;
inline constexpr uint32_t kPsrQ = 1u << 27;
inline constexpr uint32_t kApsrMask = 0xF8000000u;
inline constexpr uint32_t kPsrT = 1u << 24;
inline constexpr uint32_t kIpsrMask = 0x1FFu;
inline constexpr uint8_t kPriorityMask = 0xF0u;

inline constexpr uint32_t kExcReset = 1u;
inline constexpr uint32_t kExcNmi = 2u;
inline constexpr uint32_t kExcHardFault = 3u;
inline constexpr uint32_t kExcSvCall = 11u;
inline constexpr uint32_t kExcPendSv = 14u;
inline constexpr uint32_t kExcSysTick = 15u;
inline constexpr uint32_t kExcExternalBase = 16u;

inline constexpr uint32_t kTailChainCycles = 6u;

inline constexpr uint32_t kMaxExceptions = 128u;

inline constexpr uint8_t kControlNPriv = 1u << 0;
inline constexpr uint8_t kControlSpSel = 1u << 1;

inline constexpr uint32_t kCcrUserSetMPend = 1u << 1;
inline constexpr uint32_t kCcrUnalignTrp = 1u << 3;
inline constexpr uint32_t kCcrBfhfnmign = 1u << 8;

inline constexpr uint32_t kScrSleepOnExit = 1u << 1;
inline constexpr uint32_t kScrSleepDeep = 1u << 2;
inline constexpr uint32_t kScrSevOnPend = 1u << 4;

inline constexpr uint32_t kHfsrForced = 1u << 30;
inline constexpr uint32_t kHfsrDebugEvt = 1u << 31;
inline constexpr uint32_t kDfsrBkpt = 1u << 1;

inline constexpr uint32_t kCfsrUndefInstr = 1u << 16;
inline constexpr uint32_t kCfsrInvState = 1u << 17;
inline constexpr uint32_t kCfsrInvPc = 1u << 18;
inline constexpr uint32_t kCfsrNoCp = 1u << 19;
inline constexpr uint32_t kCfsrUnaligned = 1u << 24;
inline constexpr uint32_t kCfsrDivByZero = 1u << 25;

inline constexpr uint8_t kSleepNone = 0u;
inline constexpr uint8_t kSleepWfi = 1u;
inline constexpr uint8_t kSleepWfe = 2u;

struct State {
    std::array<uint32_t, 13> r{};
    uint32_t sp_main = 0u;
    uint32_t sp_process = 0u;
    uint32_t lr = 0u;
    uint32_t pc = 0u;

    uint32_t apsr = 0u;
    uint32_t ipsr = 0u;
    uint8_t itstate = 0u;
    bool thumb = true;
    bool handler_mode = false;
    uint8_t control = 0u;

    uint8_t primask = 0u;
    uint8_t faultmask = 0u;
    uint8_t basepri = 0u;

    uint8_t event_register = 0u;
    uint8_t sleep = kSleepNone;
    uint8_t excl_active = 0u;
    uint8_t deep_sleep = 0u;
    std::array<uint8_t, 5> pad{};

    std::array<uint8_t, kMaxExceptions> active{};
    std::array<uint8_t, kMaxExceptions> pending{};
    std::array<uint8_t, kMaxExceptions> enabled{};
    std::array<uint8_t, kMaxExceptions> priority{};
    std::array<uint8_t, kMaxExceptions> level{};

    uint32_t vtor = 0u;
    uint32_t prigroup = 0u;
    uint32_t ccr = 0x00000200u;
    uint32_t scr = 0u;
    uint32_t hfsr = 0u;
    uint32_t dfsr = 0u;
    uint32_t cfsr = 0u;
    uint32_t excl_tag = 0u;
    uint32_t load_regs = 0u;
    uint32_t sleep_exc_return = 0u;
    std::array<uint8_t, 4> pad_cycles{};

    uint32_t syst_csr = 0u;
    uint32_t syst_rvr = 0u;
    uint32_t syst_cvr = 0u;
    uint32_t syst_prescale = 0u;

    uint64_t cycles = 0u;

    template <typename F>
    static constexpr void Visit(State& s, F& field) {
        field("r", s.r);
        field("sp_main", s.sp_main);
        field("sp_process", s.sp_process);
        field("lr", s.lr);
        field("pc", s.pc);
        field("apsr", s.apsr);
        field("ipsr", s.ipsr);
        field("itstate", s.itstate);
        field("thumb", s.thumb);
        field("handler_mode", s.handler_mode);
        field("control", s.control);
        field("primask", s.primask);
        field("faultmask", s.faultmask);
        field("basepri", s.basepri);
        field("event_register", s.event_register);
        field("sleep", s.sleep);
        field("excl_active", s.excl_active);
        field("deep_sleep", s.deep_sleep);
        field.Skip(s.pad);
        field("active", s.active);
        field("pending", s.pending);
        field("enabled", s.enabled);
        field("priority", s.priority);
        field("level", s.level);
        field("vtor", s.vtor);
        field("prigroup", s.prigroup);
        field("ccr", s.ccr);
        field("scr", s.scr);
        field("hfsr", s.hfsr);
        field("dfsr", s.dfsr);
        field("cfsr", s.cfsr);
        field("excl_tag", s.excl_tag);
        field("load_regs", s.load_regs);
        field("sleep_exc_return", s.sleep_exc_return);
        field.Skip(s.pad_cycles);
        field("syst_csr", s.syst_csr);
        field("syst_rvr", s.syst_rvr);
        field("syst_cvr", s.syst_cvr);
        field("syst_prescale", s.syst_prescale);
        field("cycles", s.cycles);
    }
};

}

}
