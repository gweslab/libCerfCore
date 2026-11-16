#pragma once

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../../core/tick_scale.h"
#include "../../execution/guest_cycle_clock.h"
#include "imx6_vivante_state.h"
#include "imx6_mmio_lane.h"
#include "imx6_vivante_identity.h"
#include "imx6_vivante_mem.h"
#include "imx6_vivante_blit.h"
#include "imx6_vivante_fe.h"
#include "imx6_ccm.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include "imx6_id.h"

namespace cerfcore {

namespace imx6_vivante {

template <uint32_t kBase, VivanteCore kCore, int kIrqSpi> class Imx6Gpu : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto& bd = emu_.Get<BoardContext>();
        if (!SocId::IsImx6(bd.GetSocId())) return false;
        if (Core() != VivanteCore::Gc355Vg) return true;
        auto& variant = emu_.Get<Imx6Variant>();
        return variant.Family() == Imx6Family::DualQuad;
    }
    void OnReady() override {
        mem_ = std::make_unique<VivanteMem>(st_, emu_, Core(), IrqSpi());
        blit_ = std::make_unique<VivanteBlit>(st_, *mem_);
        fe_ = std::make_unique<VivanteFe>(st_, *mem_, *blit_);
        mem_->SetSemaphoreTokenArmedCallback([this] { OnSemaphoreTokenArmed(); });
        clock_ = &emu_.Get<GuestCycleClock>();
        fe_event_ = clock_->Add([this] { OnFrontendFetch(); });
        clock_->RegisterIdleListener([this] {
            std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
            if (!fe_->Busy()) clock_->Disarm(fe_event_);
        });
        clock_->RegisterIdleExitListener([this] { ArmAfterIdle(); });
        clock_->RegisterRateListener([this] { ArmAfterIdle(); });
        emu_.Get<Imx6Ccm>().RegisterRateListener([this] { ArmAfterIdle(); });
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            Reset();
        });
        emu_.Get<GuestCpuReset>().RegisterResetReleaseListener([this] {
            if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
            PostResetEffects();
        });
    }

    uint32_t MmioBase() const override { return kBase; }
    uint32_t MmioSize() const override { return 0x4000u; }

    VivanteCore Core() const { return kCore; }
    int IrqSpi() const { return kIrqSpi; }

    uint8_t ReadByte(uint32_t addr) override {
        const uint32_t word_off = (addr - MmioBase()) & ~3u;
        if (word_off == 0x0DCu || word_off == 0x100u)
            HaltUnsupportedAccess("read8 Vivante strict probe", addr, 0u);
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        const uint32_t word_off = (addr - MmioBase()) & ~3u;
        if (word_off == 0x0DCu || word_off == 0x100u)
            HaltUnsupportedAccess("read16 Vivante strict probe", addr, 0u);
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        const uint32_t off = addr - MmioBase();
        const auto& identity = IdentityFor(Core(), emu_.Get<Imx6Variant>().Family());
        uint32_t value = 0;
        switch (off) {
        case 0x000: {
            value = (st_.regs_[0] & 0x00F80FFFu) | (1u << 16) | (1u << 17) | (1u << 18);
            if (fe_->Busy()) {
                if (Core() == VivanteCore::Gc3202d)
                    value &= ~(1u << 17);
                else if (Core() == VivanteCore::Gc355Vg)
                    value &= ~(1u << 18);
                else
                    value &= ~(1u << 16);
            }
            break;
        }
        case 0x004: {
            value = 0x1FFu | kIdleStateBits;
            if (fe_->Busy()) {
                if (Core() == VivanteCore::Gc3202d) {
                    value &= ~((1u << 0) | (1u << 1) | (1u << 2));
                } else if (Core() == VivanteCore::Gc355Vg) {
                    value &= ~((1u << 0) | (1u << 8));
                } else {
                    value &= ~0xFFu;
                }
                value &= ~(1u << 31);
            }
            break;
        }
        case 0x010:
            value = st_.intr_status_;
            if (st_.intr_status_ != 0u) {
                st_.intr_status_ = 0u;
                mem_->UpdateIrq();
            }
            break;
        case 0x014:
            value = st_.intr_enable_;
            break;
        case 0x018:
            value = identity.chip_identity;
            break;
        case 0x01c:
            value = identity.features;
            break;
        case 0x020:
            value = identity.model;
            break;
        case 0x024:
            value = identity.revision;
            break;
        case 0x028:
            HaltUnsupportedAccess("Vivante CHIP_DATE is not read by supported i.MX6 guests", addr, identity.model);
        case 0x02c:
            if (Core() != VivanteCore::Gc3202d)
                HaltUnsupportedAccess("Vivante CHIP_TIME is not read on this core by supported i.MX6 guests", addr,
                                      identity.model);
            value = imx6_vivante::kGc320ChipTimeStub;
            break;
        case 0x034:
            value = identity.minor[0];
            break;
        case 0x048:
            value = identity.specs[0];
            break;
        case 0x074:
            value = identity.minor[1];
            break;
        case 0x080:
            value = identity.specs[1];
            break;
        case 0x084:
            value = identity.minor[2];
            break;
        case 0x088:
            value = identity.minor[3];
            break;
        case 0x08c:
            value = identity.specs[2];
            break;
        case 0x094:
            value = identity.minor[4];
            break;
        case 0x09c:
            value = identity.specs[3];
            break;
        case 0x0a0:
            value = identity.minor[5];
            break;
        case 0x0a8:
        case 0x0dc:
        case 0x470:
            value = imx6_vivante::kUserModeReadStub;
            break;
        case 0x008:
        case 0x100:
        case 0x104:
        case 0x414:
        case 0x480:
        case 0x664:
        case 0x668:
        case 0x66c:
            value = st_.regs_[off >> 2];
            break;
        default:
            if (mem_->IsGpuStateOffset(off)) {
                value = mem_->StateReg(off);
                break;
            }
            HaltUnsupportedAccess("imx6-vivante read32 unmodelled register", addr, 0);
        }
        return value;
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        if (((addr - MmioBase()) & ~3u) == 0x100u)
            HaltUnsupportedAccess("write8 Vivante PM.POWER_CONTROLS", addr, value);
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        if (((addr - MmioBase()) & ~3u) == 0x100u)
            HaltUnsupportedAccess("write16 Vivante PM.POWER_CONTROLS", addr, value);
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        fe_->SetParked(false);
        const uint32_t off = addr - MmioBase();
        switch (off) {
        case 0x000:
            if ((value & 0xFF00E000u) != 0u)
                HaltUnsupportedAccess("Vivante HI.CLOCK_CONTROL reserved bits", addr, value);
            if ((value & 0x00001000u) != 0u) {
                ResetGpuState(value);
            } else {
                st_.regs_[off >> 2] = value;
            }
            break;
        case 0x008:
            if ((value & 0xFFFF0000u) != 0u)
                HaltUnsupportedAccess("Vivante HI.AXI_CONFIG reserved bits", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x010:
            HaltUnsupportedAccess("Vivante INTR_ACKNOWLEDGE guest write is ungrounded", addr, value);
        case 0x014:
            st_.intr_enable_ = value;
            st_.regs_[off >> 2] = value;
            mem_->UpdateIrq();
            break;
        case 0x03c:
            break;
        case 0x100:
            if ((value & ~1u) != 0u)
                HaltUnsupportedAccess("Vivante PM.POWER_CONTROLS", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x104:
            if ((value & ~0x004384FFu) != 0u)
                HaltUnsupportedAccess("Vivante MODULE_CONTROLS reserved bits", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x10c:
            if ((value & ~0x00DFFF01u) != 0u && value != 0x015B0880u && value != 0x015B0881u && value != 0x01D90880u)
                HaltUnsupportedAccess("Vivante PULSE_EATER unmodelled bits", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x400:
        case 0x404:
        case 0x408:
        case 0x40c:
        case 0x410:
        case 0x418:
        case 0x41c:
        case 0x420:
        case 0x424:
        case 0x428:
            st_.regs_[off >> 2] = value;
            break;
        case 0x414:
            if ((value & ~0x003000FFu) != 0u)
                HaltUnsupportedAccess("Vivante DEBUG_MEMORY unmodelled bits", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x480:
            if ((value & ~0x000000FFu) != 0u)
                HaltUnsupportedAccess("Vivante BUS_CONFIG reserved bits", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x430:
            mem_->HaltUnsupported(kEngineCacheFlush, addr, value);
        case 0x654:
            if ((value & 7u) != 0u)
                HaltUnsupportedAccess("Vivante COMMAND_ADDRESS alignment", addr, value);
            st_.regs_[off >> 2] = value;
            break;
        case 0x658:
            if ((value & kFeCommandGuestBit21) != 0u)
                HaltUnsupportedAccess("Vivante COMMAND_CONTROL bit 21 unmodelled", addr, value);
            if ((value & ~(kFeCommandPrefetchMask | kFeCommandEnable)) != 0u)
                HaltUnsupportedAccess("Vivante COMMAND_CONTROL reserved bits", addr, value);
            st_.regs_[off >> 2] = value & ~0x00010000u;
            if (value & kFeCommandEnable) {
                fe_wait_anchor_ = clock_->Cycles();
                fe_->RunFrontend(value);
                ScheduleFrontendFetch();
            }
            break;
        default:
            if (mem_->IsGpuStateOffset(off)) {
                mem_->EnsureStateSize();
                mem_->StoreStateReg(off, value);
                VivanteBlitKick kick;
                VivanteBlit::DecodeKick(off, value, kick);
                blit_->ExecuteKick(kick);
                break;
            }
            HaltUnsupportedAccess("imx6-vivante write32 unmodelled register", addr, value);
        }
    }

    void SaveState(StateWriter& w) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        w.WriteBytes("regs", st_.regs_, sizeof(st_.regs_));
        w.Write("intr_status", st_.intr_status_);
        w.Write("intr_enable", st_.intr_enable_);
        uint32_t b = st_.irq_asserted_ ? 1u : 0u;
        w.Write("irq_asserted", b);
        b = st_.fe_live_ ? 1u : 0u;
        w.Write("fe_live", b);
        b = st_.fe_idle_ring_ ? 1u : 0u;
        w.Write("fe_idle_ring", b);
        b = st_.fe_stalled_ ? 1u : 0u;
        w.Write("fe_stalled", b);
        b = st_.fe_waiting_ ? 1u : 0u;
        w.Write("fe_waiting", b);
        w.Write("fe_ring_pc", st_.fe_ring_pc_);
        w.Write("fe_ring_prefetch", st_.fe_ring_prefetch_);
        w.Write("fe_window_words", st_.fe_window_words_);
        w.Write("fe_wait_cycles", st_.fe_wait_cycles_);
        w.Write("fe_call_depth", st_.fe_call_depth_);
        for (const FeCallFrame& frame : st_.fe_call_stack_) {
            w.Write("frame_return_address", frame.return_address);
            w.Write("frame_return_window_words", frame.return_window_words);
        }
        w.WriteBytes("semaphore_tokens", st_.semaphore_tokens_, sizeof(st_.semaphore_tokens_));
        const uint32_t n = static_cast<uint32_t>(st_.state_.size());
        w.Write("state_count", n);
        if (n) w.WriteBytes("state", st_.state_.data(), n * sizeof(uint32_t));
        b = st_.pattern_init_valid_ ? 1u : 0u;
        w.Write("pattern_init_valid", b);
        w.Write("pattern_init_config", st_.pattern_init_config_);
        w.Write("pattern_init_address", st_.pattern_init_address_);
        w.Write("pattern_init_low", st_.pattern_init_low_);
        w.Write("pattern_init_high", st_.pattern_init_high_);
        w.Write("pattern_init_mask_low", st_.pattern_init_mask_low_);
        w.Write("pattern_init_mask_high", st_.pattern_init_mask_high_);
        w.Write("pattern_init_byte_count", st_.pattern_init_byte_count_);
        w.WriteBytes("pattern_init_bytes", st_.pattern_init_bytes_, sizeof(st_.pattern_init_bytes_));
        const uint64_t fe_wait_phase = (st_.fe_waiting_ || st_.fe_idle_ring_)
                                           ? clock_->Cycles() - fe_wait_anchor_
                                           : 0u;
        w.Write<uint64_t>("fe_wait_phase", fe_wait_phase);
    }

    void RestoreState(StateReader& r) override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        r.ReadBytes("regs", st_.regs_, sizeof(st_.regs_));
        r.Read("intr_status", st_.intr_status_);
        r.Read("intr_enable", st_.intr_enable_);
        uint32_t b = 0;
        r.Read("irq_asserted", b);
        st_.irq_asserted_ = b != 0u;
        r.Read("fe_live", b);
        st_.fe_live_ = b != 0u;
        r.Read("fe_idle_ring", b);
        st_.fe_idle_ring_ = b != 0u;
        r.Read("fe_stalled", b);
        st_.fe_stalled_ = b != 0u;
        r.Read("fe_waiting", b);
        st_.fe_waiting_ = b != 0u;
        r.Read("fe_ring_pc", st_.fe_ring_pc_);
        r.Read("fe_ring_prefetch", st_.fe_ring_prefetch_);
        r.Read("fe_window_words", st_.fe_window_words_);
        r.Read("fe_wait_cycles", st_.fe_wait_cycles_);
        r.Read("fe_call_depth", st_.fe_call_depth_);
        for (FeCallFrame& frame : st_.fe_call_stack_) {
            r.Read("frame_return_address", frame.return_address);
            r.Read("frame_return_window_words", frame.return_window_words);
        }
        r.ReadBytes("semaphore_tokens", st_.semaphore_tokens_, sizeof(st_.semaphore_tokens_));
        uint32_t n = 0;
        r.Read("state_count", n);
        st_.state_.resize(n);
        if (n) r.ReadBytes("state", st_.state_.data(), n * sizeof(uint32_t));
        r.Read("pattern_init_valid", b);
        st_.pattern_init_valid_ = b != 0u;
        r.Read("pattern_init_config", st_.pattern_init_config_);
        r.Read("pattern_init_address", st_.pattern_init_address_);
        r.Read("pattern_init_low", st_.pattern_init_low_);
        r.Read("pattern_init_high", st_.pattern_init_high_);
        r.Read("pattern_init_mask_low", st_.pattern_init_mask_low_);
        r.Read("pattern_init_mask_high", st_.pattern_init_mask_high_);
        r.Read("pattern_init_byte_count", st_.pattern_init_byte_count_);
        r.ReadBytes("pattern_init_bytes", st_.pattern_init_bytes_, sizeof(st_.pattern_init_bytes_));
        r.Read("fe_wait_phase", fe_wait_anchor_);
        fe_->SetParked(false);
        clock_->Disarm(fe_event_);
    }

    void PostRestore() override {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        mem_->UpdateIrq();
        fe_wait_anchor_ = clock_->Cycles() - fe_wait_anchor_;
        ScheduleFrontendFetch();
    }

    void Reset() {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        ResetGpuState(0u);
    }

    void PostResetEffects() {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        mem_->UpdateIrq();
        ResetFrontendParkState();
        ScheduleFrontendFetch();
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (off == 0x010u) {
            std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
            HaltUnsupportedAccess("Vivante INTR_ACKNOWLEDGE guest write is ungrounded", lane.address,
                                  lane.value);
        }
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    }

private:

    void ResetGpuState(uint32_t control) {
        const bool irq_was_asserted = st_.irq_asserted_;
        st_ = VivanteState{};
        st_.irq_asserted_ = irq_was_asserted;
        mem_->UpdateIrq();
        st_.regs_[0] = control & ~0x00001000u;
        ResetFrontendParkState();
    }

    void ResetFrontendParkState() {
        fe_wait_anchor_ = clock_->Cycles();
        fe_->ResetParkedWord();
        clock_->Disarm(fe_event_);
    }

    void ScheduleFrontendFetch() {
        if (!fe_->PrepareFrontendFetch()) {
            clock_->Disarm(fe_event_);
            return;
        }
        ArmFrontendWait();
    }

    void ArmFrontendWait() {
        const uint64_t core_hz = emu_.Get<Imx6Ccm>().GpuCoreHz(Core() == VivanteCore::Gpu3d);
        const uint64_t period_cycles = ScaleU64Ceil(fe_->FrontendWaitCycles(), clock_->CpuHz(), core_hz);
        fe_fetch_at_ = fe_wait_anchor_ + period_cycles;
        clock_->Arm(fe_event_, fe_fetch_at_);
    }

    void ArmAfterIdle() {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        ScheduleFrontendFetch();
    }

    void OnFrontendFetch() {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        fe_wait_anchor_ = fe_fetch_at_;
        if (fe_->ResumeParkedWord()) {
            ArmFrontendWait();
            return;
        }
        ScheduleFrontendFetch();
    }

    void OnSemaphoreTokenArmed() {
        std::lock_guard<std::recursive_mutex> lk(fe_mutex_);
        if (fe_->AdvanceSemaphoreStall()) ScheduleFrontendFetch();
    }

    VivanteState st_;
    std::unique_ptr<VivanteMem> mem_;
    std::unique_ptr<VivanteBlit> blit_;
    std::unique_ptr<VivanteFe> fe_;
    std::recursive_mutex fe_mutex_;
    GuestCycleClock* clock_ = nullptr;
    GuestCycleClock::Event* fe_event_ = nullptr;
    uint64_t fe_wait_anchor_ = 0u;
    uint64_t fe_fetch_at_ = 0u;
};

}

}
