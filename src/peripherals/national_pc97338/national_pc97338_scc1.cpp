#include "../uart16550/uart16550.h"
#include "national_pc97338.h"
#include "national_pc97338_board.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/state/state_stream.h>

#include <cstdint>

namespace cerfcore {

namespace {

constexpr uint32_t kScc1Port   = 0x3F8u;
constexpr uint32_t kRegTxd     = 0u;
constexpr uint32_t kRegLbgdH   = 1u;
constexpr uint32_t kRegLcrBsr  = 3u;
constexpr uint8_t  kBsrBankSel = 0x80u;
constexpr uint8_t  kBsrBank1Hi = 0xC0u;
constexpr uint8_t  kBsrBank1Lo = 0x03u;
constexpr uint8_t  kLcrFormat  = 0x7Fu;
constexpr uint8_t  kIerMask    = 0x0Fu;
constexpr uint8_t  kMcrIsen    = 0x08u;
constexpr uint32_t kSccClockHz = 24000000u;
constexpr uint32_t kPrescaler  = 13u;

class NationalPc97338Scc1 : public Uart16550 {
public:
    explicit NationalPc97338Scc1(CerfEmulator& emu)
        : Uart16550(emu, Config{.ier_mask = kIerMask, .irq_gate_mcr = kMcrIsen, .irq_gate_ier = 0u,
                                .loopback_ungates_irq = true},
                    Clock{kSccClockHz, kPrescaler}) {}

    bool ShouldRegister() override {
        return emu_.TryGet<NationalPc97338Board>() != nullptr;
    }

    void OnReady() override {
        chip_      = &emu_.Get<NationalPc97338>();
        mmio_base_ = chip_->IsaIoBase() + kScc1Port;
        Uart16550::OnReady();
    }

    uint32_t MmioBase() const override { return mmio_base_; }
    uint32_t MmioSize() const override { return 8u; }

    uint8_t ReadByte(uint32_t addr) override {
        if (!chip_->Scc1Enabled()) emu_.Get<Fatal>().Die("PC97338 SCC1: read at 0x%08X while FER blocks SCC1", addr);
        const uint32_t off = addr - mmio_base_;
        if (off == kRegLcrBsr) return bsr_;
        if (Bank1() && off > kRegLbgdH) HaltUnsupportedAccess("PC97338 SCC1 bank 1 ReadByte", addr, 0);
        return static_cast<uint8_t>(Uart16550::ReadWord(addr) & 0xFFu);
    }

    void WriteByte(uint32_t addr, uint8_t value) override {
        if (!chip_->Scc1Enabled()) return;
        const uint32_t off = addr - mmio_base_;
        if (off == kRegLcrBsr) {
            WriteBsr(addr, value);
            return;
        }
        if (Bank1() && off > kRegLbgdH) HaltUnsupportedAccess("PC97338 SCC1 bank 1 WriteByte", addr, value);
        if (!Bank1() && off == kRegTxd && !chip_->ClockRunning())
            emu_.Get<Fatal>().Die("PC97338 SCC1: TXD write 0x%02X with the chip clock frozen", value);
        Uart16550::WriteWord(addr, value);
    }

    uint32_t ReadWord(uint32_t addr) override { return Peripheral::ReadWord(addr); }
    void WriteWord(uint32_t addr, uint32_t value) override { Peripheral::WriteWord(addr, value); }

    void SaveState(StateWriter& w) override {
        Uart16550::SaveState(w);
        w.Write("bsr", bsr_);
    }

    void RestoreState(StateReader& r) override {
        Uart16550::RestoreState(r);
        r.Read("bsr", bsr_);
    }

protected:
    uint32_t    RegStride() const override { return 1u; }
    const char* Name()      const override { return "COM1"; }

    void SetInterruptLine(bool pending) override {
        if (!pending) return;
        emu_.Get<Fatal>().Die("PC97338 SCC1 at 0x%08X raised its interrupt line; its routing on this board "
                              "is not modeled", mmio_base_);
    }

    void OnLineReset(ResetLineKind kind) override {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        bsr_ = 0u;
        Uart16550::OnLineReset(kind);
    }

private:
    bool Bank1() const { return (bsr_ & kBsrBankSel) != 0u; }

    static bool SelectsBank1(uint8_t value) {
        if ((value & kBsrBank1Hi) == kBsrBankSel) return true;
        return (value & kBsrBank1Hi) == kBsrBank1Hi && (value & kBsrBank1Lo) != 0u;
    }

    void WriteBsr(uint32_t addr, uint8_t value) {
        if ((value & kBsrBankSel) == 0u) {
            bsr_ = value;
            Uart16550::WriteWord(addr, value);
            return;
        }
        if (!SelectsBank1(value)) HaltUnsupportedAccess("PC97338 SCC1 extended bank select", addr, value);
        bsr_ = value;
        const uint8_t lcr = static_cast<uint8_t>(Uart16550::ReadWord(addr) & kLcrFormat);
        Uart16550::WriteWord(addr, static_cast<uint8_t>(lcr | kBsrBankSel));
    }

    NationalPc97338* chip_      = nullptr;
    uint32_t         mmio_base_ = 0u;
    uint8_t          bsr_       = 0u;
};

}

REGISTER_SERVICE(NationalPc97338Scc1);

}
