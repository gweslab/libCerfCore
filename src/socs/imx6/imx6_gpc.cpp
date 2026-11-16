#include "imx6_gpc.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/state_stream.h>
#include "imx6_ccm.h"
#include "imx6_gic.h"
#include "imx6_id.h"
#include "imx6_mmio_lane.h"
#include "../../execution/guest_cycle_clock.h"
#include "../../peripherals/peripheral_dispatcher.h"

namespace cerfcore {

namespace {

constexpr uint32_t kGpcCntr = 0x000u;
constexpr uint32_t kGpcPgr = 0x004u;
constexpr uint32_t kGpcCntrReset = 0x00100000u;
constexpr uint32_t kGpcCntrIrqMask = 0x00200000u;
constexpr uint32_t kGpcCntrPowerRequestMask = 0x00000003u;
constexpr uint32_t kGpcPgrDebugClockMask = 0x60000000u;
constexpr uint32_t kPgcGpuCtrl = 0x260u;
constexpr uint32_t kPgcGpuPupScr = 0x264u;
constexpr uint32_t kPgcGpuPdnScr = 0x268u;
constexpr uint32_t kPgcGpuSr = 0x26Cu;
constexpr uint32_t kPgcPuOffset10 = 0x270u;
constexpr uint32_t kPgcPuOffset10Read = 0u; /* Undocumented. */
constexpr uint32_t kPgcPuOffset10GuestWrite = 1u;
constexpr uint32_t kPgcCpuCtrl = 0x2A0u;
constexpr uint32_t kPgcCpuSr = 0x2ACu;
constexpr uint32_t kPgcGpuCtrlMask = 0x00000001u;
constexpr uint32_t kPgcCpuCtrlPcr = 0x00000001u;
constexpr uint32_t kPgcCpuSrPsr = 0x00000001u;
constexpr uint32_t kPgcGpuPupScrMask = 0x00003F3Fu;
constexpr uint32_t kPgcGpuPupScrReset = 0x00000F01u;
constexpr uint32_t kPgcGpuPdnScrReset = 0x00000101u;
constexpr uint32_t kPgcGpuPdnScrMask = 0x00003F3Fu;
constexpr uint32_t kPgcGpuSrPowerStatus = 0x00000001u;

}

bool Imx6Gpc::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

void Imx6Gpc::OnReady() {
    Reset();
    emu_.Get<GuestDeepSleep>().RegisterWaker(this);
    emu_.Get<GuestDeepSleep>().RegisterParkWakeSource([this] { return HasWakePending(); });
    emu_.Get<GuestCycleClock>().RegisterIdleListener([this] { OnIdle(); });
    emu_.Get<GuestCycleClock>().RegisterIdleExitListener(
        [this] {
            in_low_power_.store(false, std::memory_order_release);
            if (clock_gated_.exchange(false, std::memory_order_acq_rel))
                emu_.Get<Imx6Gic>().SetLowPowerMode(0u);
        });
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        Reset();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint8_t Imx6Gpc::ReadByte(uint32_t address) {
    return Imx6ReadMmioByte(address, [this](uint32_t aligned) { return ReadWord(aligned); });
}

uint16_t Imx6Gpc::ReadHalf(uint32_t address) {
    return Imx6ReadMmioHalf(address, [this](uint32_t aligned) { return ReadWord(aligned); });
}

uint32_t Imx6Gpc::ReadWord(uint32_t address) {
    return ReadRegister(address - MmioBase());
}

void Imx6Gpc::WriteByte(uint32_t address, uint8_t value) {
    Imx6ForEachMmioLane(address, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) {
        WriteRegister(lane.address - MmioBase(), lane.Merge(ReadWord(lane.address)));
    });
}

void Imx6Gpc::WriteHalf(uint32_t address, uint16_t value) {
    Imx6ForEachMmioLane(address, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) {
        WriteRegister(lane.address - MmioBase(), lane.Merge(ReadWord(lane.address)));
    });
}

void Imx6Gpc::WriteWord(uint32_t address, uint32_t value) {
    WriteRegister(address - MmioBase(), value);
}

void Imx6Gpc::SaveState(StateWriter& writer) {
    uint32_t masks[4]{};
    for (uint32_t i = 0u; i < 4u; ++i) masks[i] = imr_[i].load(std::memory_order_acquire);
    writer.WriteBytes("imr", masks, sizeof(masks));
    writer.Write("cntr_irq_mask", cntr_irq_mask_.load(std::memory_order_acquire));
    writer.Write("pgr", pgr_);
    writer.Write("pgc_gpu_ctrl", pgc_gpu_ctrl_);
    writer.Write("pgc_gpu_pupscr", pgc_gpu_pupscr_);
    writer.Write("pgc_gpu_pdn_scr", pgc_gpu_pdn_scr_);
    writer.Write("pgc_gpu_sr", pgc_gpu_sr_);
    writer.Write("pgc_cpu_ctrl", pgc_cpu_ctrl_);
    writer.Write("pgc_cpu_sr", pgc_cpu_sr_);
}

void Imx6Gpc::RestoreState(StateReader& reader) {
    uint32_t masks[4]{};
    reader.ReadBytes("imr", masks, sizeof(masks));
    for (uint32_t i = 0u; i < 4u; ++i) imr_[i].store(masks[i], std::memory_order_release);
    uint32_t cntr_irq_mask = 0u;
    reader.Read("cntr_irq_mask", cntr_irq_mask);
    cntr_irq_mask_.store(cntr_irq_mask, std::memory_order_release);
    reader.Read("pgr", pgr_);
    reader.Read("pgc_gpu_ctrl", pgc_gpu_ctrl_);
    reader.Read("pgc_gpu_pupscr", pgc_gpu_pupscr_);
    reader.Read("pgc_gpu_pdn_scr", pgc_gpu_pdn_scr_);
    reader.Read("pgc_gpu_sr", pgc_gpu_sr_);
    reader.Read("pgc_cpu_ctrl", pgc_cpu_ctrl_);
    reader.Read("pgc_cpu_sr", pgc_cpu_sr_);
    in_low_power_.store(false, std::memory_order_release);
    clock_gated_.store(false, std::memory_order_release);
}

void Imx6Gpc::Reset() {
    for (auto& mask : imr_) mask.store(0u, std::memory_order_release);
    cntr_irq_mask_.store(0u, std::memory_order_release);
    in_low_power_.store(false, std::memory_order_release);
    clock_gated_.store(false, std::memory_order_release);
    pgr_ = 0u;
    pgc_gpu_ctrl_ = 0u;
    pgc_gpu_pupscr_ = kPgcGpuPupScrReset;
    pgc_gpu_pdn_scr_ = kPgcGpuPdnScrReset;
    pgc_gpu_sr_ = 0u;
    pgc_cpu_ctrl_ = 0u;
    pgc_cpu_sr_ = 0u;
}

void Imx6Gpc::OnIdle() {
    const uint32_t mode = emu_.Get<Imx6Ccm>().WfiLowPowerMode();
    if (mode == 0u) {
        in_low_power_.store(false, std::memory_order_release);
        if (clock_gated_.exchange(false, std::memory_order_acq_rel))
            emu_.Get<Imx6Gic>().SetLowPowerMode(0u);
        return;
    }
    if (mode == 1u || mode == 2u) {
        in_low_power_.store(true, std::memory_order_release);
        if (mode == 2u) {
            if (!clock_gated_.exchange(true, std::memory_order_acq_rel))
                emu_.Get<Imx6Gic>().SetLowPowerMode(2u);
        } else if (clock_gated_.exchange(false, std::memory_order_acq_rel)) {
            emu_.Get<Imx6Gic>().SetLowPowerMode(0u);
        }
        if (mode == 2u && (pgc_cpu_ctrl_ & kPgcCpuCtrlPcr) != 0u) {
            pgc_cpu_sr_ |= kPgcCpuSrPsr;
            emu_.Get<GuestDeepSleep>().Enter();
        }
        return;
    }
    emu_.Get<Fatal>().Die("Imx6Gpc: CCM_CLPCR.LPM value %u is reserved", mode);
}

bool Imx6Gpc::AllowsWake(uint32_t irq) const {
    if (!in_low_power_.load(std::memory_order_acquire) || irq < 32u || irq > 159u) return true;
    if (cntr_irq_mask_.load(std::memory_order_acquire) != 0u) return false;
    const uint32_t source = irq - 32u;
    return (imr_[source >> 5u].load(std::memory_order_acquire) & (1u << (source & 31u))) == 0u;
}

bool Imx6Gpc::HasWakePending() const {
    if (cntr_irq_mask_.load(std::memory_order_acquire) != 0u) return false;
    for (uint32_t index = 0u; index < 4u; ++index) {
        const uint32_t pending = emu_.Get<Imx6Gic>().ReadMmio(0x1204u + index * 4u);
        const uint32_t unmasked = ~imr_[index].load(std::memory_order_acquire);
        if ((pending & unmasked) != 0u) return true;
    }
    return false;
}

void Imx6Gpc::LatchSleepWakeCause() {}

void Imx6Gpc::ClearSleepWakeCause() { pgc_cpu_sr_ &= ~kPgcCpuSrPsr; }

uint32_t Imx6Gpc::ReadRegister(uint32_t offset) {
    if ((offset & 3u) != 0u) HaltUnsupportedAccess("unaligned access to a GPC register", MmioBase() + offset, 0);
    if (offset == kGpcCntr) return kGpcCntrReset | cntr_irq_mask_;
    if (offset == kGpcPgr) return pgr_;
    if (offset >= 0x08u && offset <= 0x14u) return imr_[(offset - 0x08u) >> 2].load(std::memory_order_acquire);
    if (offset >= 0x18u && offset <= 0x24u)
        return emu_.Get<Imx6Gic>().ReadMmio(0x1204u + offset - 0x18u);
    if (offset == kPgcGpuCtrl) return pgc_gpu_ctrl_;
    if (offset == kPgcGpuPupScr) return pgc_gpu_pupscr_;
    if (offset == kPgcGpuPdnScr) return pgc_gpu_pdn_scr_;
    if (offset == kPgcGpuSr) return pgc_gpu_sr_;
    if (offset == kPgcCpuCtrl) return pgc_cpu_ctrl_;
    if (offset == kPgcCpuSr) return pgc_cpu_sr_;
    if (offset == kPgcPuOffset10) return kPgcPuOffset10Read;
    HaltUnsupportedAccess("read of unmodelled fields in GPC register", MmioBase() + offset, 0);
}

void Imx6Gpc::WriteRegister(uint32_t offset, uint32_t value) {
    if ((offset & 3u) != 0u) HaltUnsupportedAccess("unaligned access to a GPC register", MmioBase() + offset, value);
    if (offset == kGpcCntr) {
        if ((value & kGpcCntrPowerRequestMask) != 0u)
            HaltUnsupportedAccess("GPC_CNTR.gpu_vpu_pdn_req/gpu_vpu_pup_req power sequence", MmioBase() + offset, value);
        cntr_irq_mask_.store(value & kGpcCntrIrqMask, std::memory_order_release);
        return;
    }
    if (offset == kGpcPgr) {
        const uint32_t debug_clock = value & kGpcPgrDebugClockMask;
        if (debug_clock == 0x40000000u || debug_clock == 0x60000000u)
            HaltUnsupportedAccess("GPC_PGR.DRCIC restricted encoding", MmioBase() + offset, value);
        pgr_ = debug_clock;
        return;
    }
    if (offset >= 0x08u && offset <= 0x14u) {
        imr_[(offset - 0x08u) >> 2].store(value, std::memory_order_release);
        return;
    }
    if (offset == kPgcGpuCtrl) {
        pgc_gpu_ctrl_ = value & kPgcGpuCtrlMask;
        return;
    }
    if (offset == kPgcGpuPupScr) {
        const uint32_t stored = value & kPgcGpuPupScrMask;
        if ((stored & 0x3Fu) == 0u)
            HaltUnsupportedAccess("PGC_GPU_PUPSCR.SW zero timing", MmioBase() + offset, value);
        if (((stored >> 8) & 0x3Fu) == 0u)
            HaltUnsupportedAccess("PGC_GPU_PUPSCR.SW2ISO zero timing", MmioBase() + offset, value);
        pgc_gpu_pupscr_ = stored;
        return;
    }
    if (offset == kPgcGpuPdnScr) {
        const uint32_t stored = value & kPgcGpuPdnScrMask;
        if ((stored & 0x3Fu) == 0u)
            HaltUnsupportedAccess("PGC_GPU_PDNSCR.ISO zero timing", MmioBase() + offset, value);
        if (((stored >> 8) & 0x3Fu) == 0u)
            HaltUnsupportedAccess("PGC_GPU_PDNSCR.ISO2SW zero timing", MmioBase() + offset, value);
        pgc_gpu_pdn_scr_ = stored;
        return;
    }
    if (offset == kPgcGpuSr) {
        if ((value & ~kPgcGpuSrPowerStatus) != 0u)
            HaltUnsupportedAccess("PGC_GPU_SR reserved bits", MmioBase() + offset, value);
        if ((value & kPgcGpuSrPowerStatus) != 0u) pgc_gpu_sr_ &= ~kPgcGpuSrPowerStatus;
        return;
    }
    if (offset == kPgcCpuCtrl) {
        if ((value & ~kPgcCpuCtrlPcr) != 0u)
            HaltUnsupportedAccess("PGC_CPU_CTRL reserved bits", MmioBase() + offset, value);
        pgc_cpu_ctrl_ = value & kPgcCpuCtrlPcr;
        return;
    }
    if (offset == kPgcCpuSr) {
        if ((value & ~kPgcCpuSrPsr) != 0u)
            HaltUnsupportedAccess("PGC_CPU_SR reserved bits", MmioBase() + offset, value);
        if ((value & kPgcCpuSrPsr) != 0u) pgc_cpu_sr_ &= ~kPgcCpuSrPsr;
        return;
    }
    if (offset == kPgcPuOffset10) {
        if (value != kPgcPuOffset10GuestWrite)
            HaltUnsupportedAccess("PGC_PU unimplemented offset 0x270 write value", MmioBase() + offset, value);
        return;
    }
    if (offset >= 0x18u && offset <= 0x24u)
        HaltUnsupportedAccess("GPC_ISR registers are read-only", MmioBase() + offset, value);
    HaltUnsupportedAccess("write to an unmodelled or reserved GPC register", MmioBase() + offset, value);
}

REGISTER_SERVICE(Imx6Gpc);

}
