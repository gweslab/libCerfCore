#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../peripherals/peripheral_dispatcher.h"
#include <cerfcore/state/state_stream.h>
#include <cerfcore/socs/guest_cpu_reset.h>

#include <cstdint>
#include <mutex>
#include <string_view>
#include <typeinfo>

namespace cerfcore {

namespace cerf_reg_window_detail {

constexpr uint32_t kMaxRegs = 36u;

enum class ReadKind : uint8_t {
    kFatal,
    kStored,
    kZero,
    kSampled,
};

struct NoLock {
    void lock() {}
    void unlock() {}
};

enum class WriteKind : uint8_t {
    kFatal,
    kStored,
    kClear,
    kDrop,
    kHooked,
};

enum class OtherReset : uint8_t {
    kReset,
    kRetain,
};

struct RegSpec {
    ReadKind   read         = ReadKind::kFatal;
    WriteKind  write        = WriteKind::kFatal;
    uint16_t   wmask        = 0u;
    uint16_t   reset        = 0u;
    uint16_t   fatal_on_set = 0u;
    OtherReset other_reset  = OtherReset::kReset;
    uint16_t   undefined_on_other_reset = 0u;
    uint16_t   undefined_on_power_on    = 0u;
    bool       low_lane                 = false;
};

struct RegWindowModel {
    uint32_t      base;
    uint32_t      size;
    uint32_t      num_regs;
    bool          word_pairs;
    RegSpec reg[kMaxRegs];
    uint32_t      reg_bytes = 2u;
    bool          word_access_only = false;
    bool          byte_lanes       = false;
};

struct RegAccessNames {
    const char* read;
    const char* write;
    const char* undefined_read;
    const char* trigger_write;
    const char* rfu_write;
};

inline constexpr RegAccessNames kByteAccessNames{
    "ReadByte", "WriteByte", "ReadByte of bits the Other-resets row leaves Undefined",
    "WriteByte sets an unmodeled trigger bit", "WriteByte sets a write-0 RFU bit"};

inline constexpr RegAccessNames kHalfAccessNames{
    "ReadHalf", "WriteHalf", "ReadHalf of bits the Other-resets row leaves Undefined",
    "WriteHalf sets an unmodeled trigger bit", "WriteHalf sets a write-0 RFU bit"};

inline constexpr RegAccessNames kWordAccessNames{
    "ReadWord", "WriteWord", "ReadWord of bits the Other-resets row leaves Undefined",
    "WriteWord sets an unmodeled trigger bit", "WriteWord sets a write-0 RFU bit"};

template <const RegWindowModel& M, class Base = Peripheral, class Lock = NoLock>
class RegWindow : public Base {
public:
    using Base::Base;

    void OnReady() override {
        ResetRegisterFile(true);
        this->emu_.template Get<PeripheralDispatcher>().Register(this);
        this->emu_.template Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind kind) {
            std::lock_guard<Lock> lk(lock_);
            ResetRegisterFile(kind == ResetLineKind::Rtc);
            AfterReset();
        });
    }

    uint32_t MmioBase() const override { return M.base; }
    uint32_t MmioSize() const override { return M.size; }

    uint16_t ReadHalf(uint32_t addr) override {
        if constexpr (M.reg_bytes != 2u || M.word_access_only) {
            this->HaltUnsupportedAccess("ReadHalf", addr, 0);
        } else {
            return ReadRegister(addr, GuestAccess{addr, 0u});
        }
    }

    void WriteHalf(uint32_t addr, uint16_t value) override {
        if constexpr (M.reg_bytes != 2u || M.word_access_only) {
            this->HaltUnsupportedAccess("WriteHalf", addr, value);
        } else {
            WriteRegister(addr, value, GuestAccess{addr, value});
        }
    }

    uint32_t ReadWord(uint32_t addr) override {
        if constexpr (!M.word_pairs) {
            this->HaltUnsupportedAccess("ReadWord", addr, 0);
        } else {
            PairLow(addr, "ReadWord", 0u);
            return this->ReadWordAsHalves(addr);
        }
    }

    void WriteWord(uint32_t addr, uint32_t value) override {
        if constexpr (!M.word_pairs) {
            this->HaltUnsupportedAccess("WriteWord", addr, value);
        } else {
            PairLow(addr, "WriteWord", value);
            this->WriteWordAsHalves(addr, value);
        }
    }

    uint8_t ReadByte(uint32_t addr) override {
        if constexpr (M.byte_lanes) {
            const uint32_t shift = (addr & 1u) * 8u;
            const uint16_t lane  = static_cast<uint16_t>(0x00FFu << shift);
            return static_cast<uint8_t>(
                ReadRegister(addr & ~1u, GuestAccess{addr, 0u, 0u, &kByteAccessNames, lane}) >> shift);
        } else {
            return static_cast<uint8_t>(
                ReadRegister(addr, GuestAccess{addr, 0u, 0u, &kByteAccessNames, 0xFFFFu, M.reg_bytes != 1u}));
        }
    }

    void WriteByte(uint32_t addr, uint8_t v) override {
        if constexpr (M.byte_lanes) {
            WriteLane(addr, v);
        } else {
            WriteRegister(addr, v, GuestAccess{addr, v, 0u, &kByteAccessNames, 0xFFFFu, M.reg_bytes != 1u});
        }
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<Lock> lk(lock_);
        for (uint32_t i = 0; i < M.num_regs; ++i) w.Write(RegStateName(i), reg_[i]);
        for (uint32_t i = 0; i < M.num_regs; ++i) w.Write("undefined", undefined_[i]);
    }
    void RestoreState(StateReader& r) override {
        std::lock_guard<Lock> lk(lock_);
        for (uint32_t i = 0; i < M.num_regs; ++i) r.Read(RegStateName(i), reg_[i]);
        for (uint32_t i = 0; i < M.num_regs; ++i) r.Read("undefined", undefined_[i]);
    }

protected:
    uint32_t ReadWordAsHalves(uint32_t addr) override {
        if constexpr (M.word_access_only) {
            PairLow(addr, "ReadWord", 0u);
            const uint32_t lo = ReadRegister(addr, GuestAccess{addr, 0u, 0u});
            return lo | (static_cast<uint32_t>(ReadRegister(addr + 2u, GuestAccess{addr, 0u, 16u})) << 16);
        } else {
            return Base::ReadWordAsHalves(addr);
        }
    }

    void WriteWordAsHalves(uint32_t addr, uint32_t value) override {
        if constexpr (M.word_access_only) {
            PairLow(addr, "WriteWord", value);
            WriteRegister(addr, static_cast<uint16_t>(value), GuestAccess{addr, value, 0u});
            WriteRegister(addr + 2u, static_cast<uint16_t>(value >> 16), GuestAccess{addr, value, 16u});
        } else {
            Base::WriteWordAsHalves(addr, value);
        }
    }

    virtual const char* RegStateName(uint32_t i) const {
        (void)i;
        return "reg";
    }

    virtual uint16_t ResetValue(uint32_t i, bool rtc) const {
        (void)rtc;
        return M.reg[i].reset;
    }
    virtual void AfterReset() {}
    virtual void BeforeStore(uint32_t i, uint16_t value) {
        (void)i;
        (void)value;
    }
    virtual void AfterStore(uint32_t i) { (void)i; }
    virtual uint16_t SampleRegister(uint32_t i, uint32_t addr) {
        (void)i;
        this->HaltUnsupportedAccess(kNames.read, addr, 0);
    }
    virtual void WriteHooked(uint32_t i, uint32_t addr, uint16_t value) {
        (void)i;
        this->HaltUnsupportedAccess(kNames.write, addr, value);
    }

    uint16_t StoredReg(uint32_t i) const { return reg_[i]; }

    void SetStoredReg(uint32_t i, uint16_t value) { reg_[i] = value; }

    void ResetRegisterFile(bool rtc) {
        for (uint32_t i = 0; i < M.num_regs; ++i) {
            if (rtc || M.reg[i].other_reset == OtherReset::kReset) reg_[i] = ResetValue(i, rtc);
            undefined_[i] = rtc ? M.reg[i].undefined_on_power_on : M.reg[i].undefined_on_other_reset;
        }
    }

    void ApplyStoredWrite(uint32_t offset, uint16_t value) {
        const uint32_t i = offset / M.reg_bytes;
        if ((offset % M.reg_bytes) != 0u || i >= M.num_regs || M.reg[i].write != WriteKind::kStored ||
            (value & ~M.reg[i].wmask) != 0u || (value & M.reg[i].fatal_on_set) != 0u) {
            this->emu_.template Get<Fatal>().Die("%s: stored write 0x%04X at offset 0x%02X is not a "
                                  "stored-register write", typeid(*this).name(), value,
                                  offset);
        }
        StoreMasked(i, value, 0xFFFFu);
    }

    Lock lock_;

private:
    static constexpr const RegAccessNames& kNames = M.word_access_only ? kWordAccessNames
                                                    : M.reg_bytes == 1u ? kByteAccessNames
                                                                        : kHalfAccessNames;

    struct GuestAccess {
        uint32_t              addr;
        uint32_t              value;
        uint32_t              half_shift = 0u;
        const RegAccessNames* names      = &kNames;
        uint16_t              lanes      = 0xFFFFu;
        bool                  low_lane   = false;
    };

    uint16_t ReadRegister(uint32_t addr, GuestAccess guest) {
        std::lock_guard<Lock> lk(lock_);
        const uint32_t i = RegIndex(addr, guest.names->read, guest);
        if (guest.low_lane && !M.reg[i].low_lane) this->HaltUnsupportedAccess(guest.names->read, guest.addr, 0);
        if ((undefined_[i] & guest.lanes) != 0u) {
            this->HaltUnsupportedAccess(guest.names->undefined_read, guest.addr,
                                        static_cast<uint32_t>(undefined_[i] & guest.lanes) << guest.half_shift);
        }
        switch (M.reg[i].read) {
            case ReadKind::kStored:  return reg_[i];
            case ReadKind::kZero:    return 0u;
            case ReadKind::kSampled: return SampleRegister(i, guest.addr);
            default: this->HaltUnsupportedAccess(guest.names->read, guest.addr, 0);
        }
    }

    void WriteRegister(uint32_t addr, uint16_t value, GuestAccess guest) {
        std::lock_guard<Lock> lk(lock_);
        const uint32_t i = RegIndex(addr, guest.names->write, guest);
        if (guest.low_lane && !M.reg[i].low_lane) {
            this->HaltUnsupportedAccess(guest.names->write, guest.addr, guest.value);
        }
        WriteIndexed(i, M.reg[i].low_lane ? static_cast<uint16_t>(value & 0x00FFu) : value, guest);
    }

    void WriteLane(uint32_t addr, uint8_t value) {
        const uint32_t shift = (addr & 1u) * 8u;
        const uint16_t lane  = static_cast<uint16_t>(0x00FFu << shift);
        const uint16_t bits  = static_cast<uint16_t>(value << shift);
        const GuestAccess guest{addr, value, 0u, &kByteAccessNames, lane};
        std::lock_guard<Lock> lk(lock_);
        const uint32_t i = RegIndex(addr & ~1u, guest.names->write, guest);
        if (M.reg[i].write == WriteKind::kHooked) this->HaltUnsupportedAccess(guest.names->write, addr, value);
        WriteIndexed(i, M.reg[i].write == WriteKind::kStored ? static_cast<uint16_t>((reg_[i] & ~lane) | bits) : bits,
                     guest);
    }

    void WriteIndexed(uint32_t i, uint16_t value, GuestAccess guest) {
        if (value & M.reg[i].fatal_on_set & guest.lanes) {
            this->HaltUnsupportedAccess((value & M.reg[i].fatal_on_set & M.reg[i].wmask & guest.lanes) != 0u
                                            ? guest.names->trigger_write
                                            : guest.names->rfu_write,
                                        guest.addr, guest.value);
        }
        switch (M.reg[i].write) {
            case WriteKind::kStored:
                BeforeStore(i, static_cast<uint16_t>(value & M.reg[i].wmask));
                StoreMasked(i, value, guest.lanes);
                AfterStore(i);
                return;
            case WriteKind::kClear:
                reg_[i] = static_cast<uint16_t>(reg_[i] & ~(value & M.reg[i].wmask & guest.lanes));
                return;
            case WriteKind::kDrop:
                return;
            case WriteKind::kHooked:
                WriteHooked(i, guest.addr, static_cast<uint16_t>(value & M.reg[i].wmask));
                return;
            default: this->HaltUnsupportedAccess(guest.names->write, guest.addr, guest.value);
        }
    }

    void StoreMasked(uint32_t i, uint16_t value, uint16_t lanes) {
        const uint16_t mask = static_cast<uint16_t>(M.reg[i].wmask & lanes);
        reg_[i]             = static_cast<uint16_t>((reg_[i] & ~mask) | (value & mask));
        undefined_[i]       = static_cast<uint16_t>(undefined_[i] & ~mask);
    }

    uint32_t RegIndex(uint32_t addr, const char* what, GuestAccess guest) {
        const uint32_t off = addr - M.base;
        if (off >= M.num_regs * M.reg_bytes || (off % M.reg_bytes) != 0u) {
            this->HaltUnsupportedAccess(what, guest.addr, guest.value);
        }
        return off / M.reg_bytes;
    }
    uint32_t PairLow(uint32_t addr, const char* what, uint32_t value) {
        const uint32_t lo = RegIndex(addr, what, GuestAccess{addr, value});
        if (lo + 1u >= M.num_regs) this->HaltUnsupportedAccess(what, addr, value);
        return lo;
    }

    uint16_t reg_[kMaxRegs]       = {};
    uint16_t undefined_[kMaxRegs] = {};
};

template <const std::string_view& Soc, const RegWindowModel& M, class Base = Peripheral>
class RegWindowBase : public RegWindow<M, Base> {
public:
    using RegWindow<M, Base>::RegWindow;

    bool ShouldRegister() override {
        return this->emu_.template Get<BoardContext>().GetSocId() == Soc;
    }
};

}

}
