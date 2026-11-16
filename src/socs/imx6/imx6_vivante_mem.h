#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx6_gic.h"
#include "imx6_vivante_mmu.h"
#include "imx6_vivante_identity.h"
#include "imx6_vivante_state.h"
#include "imx6_vivante_state_registers.h"

namespace cerfcore {

namespace imx6_vivante {

class VivanteMem {
public:
    VivanteMem(VivanteState& s, CerfEmulator& emu, VivanteCore core, int irq_spi)
        : s_(s), emu_(emu), core_(core), irq_spi_(irq_spi),
          mmu_(s, emu), state_registers_(s, *this) {}

    struct MaskedStateGroup {
        uint32_t field_mask;
        uint32_t preserve_mask;
    };

    VivanteCore Core() const { return core_; }
    bool Is2d() const { return core_ == VivanteCore::Gc3202d; }
    bool UsesDeRotationAngle() const {
        return (IdentityFor(core_, emu_.Get<Imx6Variant>().Family()).minor[1] & kVivanteMinorFeatureNonPowerOfTwo) != 0u;
    }
    uint32_t PixelPipes() const {
        const uint32_t specs = IdentityFor(core_, emu_.Get<Imx6Variant>().Family()).specs[0];
        const uint32_t pipes = (specs >> 25u) & 7u;
        if (pipes == 0u) HaltUnsupported("imx6-vivante pixel pipe count of a core whose SPECS has none", 0x048u, specs);
        return pipes;
    }

    [[noreturn]] void HaltUnsupported(const char* op, uint32_t addr, uint64_t value) const {
        emu_.Get<Fatal>().Die("Vivante core %u rejected %s at 0x%08X (value 0x%016llX)", static_cast<uint32_t>(core_),
                              op, addr, static_cast<unsigned long long>(value));
    }

    bool DetectIdleRing(uint32_t pc, IdleRingInfo& info) const;

    static bool IsGpuStateOffset(uint32_t off) {
        return VivanteStateRegisters::SupportsOffset(off);
    }

    void EnsureStateSize() {
        if (s_.state_.empty()) s_.state_.resize(kMaxStateBytes / 4u);
    }

    uint32_t StateReg(uint32_t byte_off) const {
        const uint32_t idx = byte_off >> 2;
        return idx < s_.state_.size() ? s_.state_[idx] : 0u;
    }

    void ArmSemaphoreToken(uint32_t token) {
        const uint32_t from = token & 0x1Fu;
        const uint32_t to = (token >> 8) & 0x1Fu;
        s_.semaphore_tokens_[to] |= 1u << from;
    }

    bool TryConsumeSemaphoreToken(uint32_t token) {
        const uint32_t from = token & 0x1Fu;
        const uint32_t to = (token >> 8) & 0x1Fu;
        const uint32_t bit = 1u << from;
        if ((s_.semaphore_tokens_[to] & bit) == 0u) return false;
        s_.semaphore_tokens_[to] &= ~bit;
        return true;
    }

    void SetSemaphoreTokenArmedCallback(std::function<void()> callback) {
        semaphore_token_armed_callback_ = std::move(callback);
    }

    void NotifySemaphoreTokenArmed() const {
        if (semaphore_token_armed_callback_) semaphore_token_armed_callback_();
    }

    template <size_t N>
    static uint32_t MergeMaskedState(uint32_t current, uint32_t value, const MaskedStateGroup (&groups)[N]) {
        for (const auto& group : groups) {
            if ((value & group.preserve_mask) == 0u) {
                current = (current & ~group.field_mask) | (value & group.field_mask);
            }
        }
        return current;
    }

    void StoreStateReg(uint32_t byte_off, uint32_t value);

    const uint8_t* TranslateCommandToHost(uint32_t address, size_t size) const;
    const uint8_t* CommandPageTableEntryToHost(uint32_t address) const {
        return mmu_.PageTableEntryToHost(address, MmuClient::Fe);
    }
    bool ReadCommandBytes(uint32_t address, void* out_buffer, size_t count) const;
    bool ReadCommandWords(uint32_t address, uint32_t* out, uint32_t count) const;
    bool FetchCommandWords(uint32_t address, uint32_t* out, uint32_t count) const;
    bool ReadGpuBytes(uint32_t address, void* out_buffer, size_t count, MmuClient client = MmuClient::Texture) const;
    bool WriteGpuBytes(uint32_t address, const void* in_buffer, size_t count,
                       MmuClient client = MmuClient::PixelEngine) const;
    const uint8_t* TranslateGpuToHost(uint32_t gpu_addr, size_t size,
                                      MmuClient client = MmuClient::Texture) const;
    uint8_t* TranslateGpuToHostWrite(uint32_t gpu_addr, size_t size,
                                     MmuClient client = MmuClient::PixelEngine) const;

    void RaiseInterrupt(uint32_t bits) const {
        s_.intr_status_ |= bits;
        UpdateIrq();
    }

    void RaiseEvent(uint32_t value) const {
        RaiseInterrupt(1u << value);
    }

    void UpdateIrq() const {
        const bool asserted = (s_.intr_status_ & s_.intr_enable_) != 0u;
        if (asserted == s_.irq_asserted_) return;
        s_.irq_asserted_ = asserted;
        auto& gic = emu_.Get<Imx6Gic>();
        if (asserted)
            gic.AssertSpi(irq_spi_);
        else
            gic.DeAssertSpi(irq_spi_);
    }

private:
    bool TransferGpuBytes(uint32_t address, const void* input_buffer, void* output_buffer, size_t count,
                         MmuClient client) const;
    VivanteState& s_;
    CerfEmulator& emu_;
    VivanteCore core_;
    int irq_spi_;
    VivanteMmu mmu_;
    VivanteStateRegisters state_registers_;
    std::function<void()> semaphore_token_armed_callback_;
};

}

}
