#include "imx6_ipu_interrupt_controller.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "imx6_gic.h"
#include "imx6_ipu_register_map.h"
#include "imx6_id.h"

namespace cerfcore {

bool Imx6IpuInterruptController::ShouldRegister() {
    auto* board = emu_.TryGet<BoardContext>();
    return board && SocId::IsImx6(board->GetSocId());
}

uint32_t Imx6IpuInterruptController::ReadRegister(uint32_t offset) const {
    if (imx6_ipu::IsIpuIntStat(offset))
        return status_[(offset - imx6_ipu::kOffIntStat0) >> 2];
    return control_[(offset - imx6_ipu::kOffIntCtrl0) >> 2];
}

void Imx6IpuInterruptController::ClearStatus(uint32_t offset, uint32_t mask) {
    status_[(offset - imx6_ipu::kOffIntStat0) >> 2] &= ~mask;
    UpdateLines();
}

void Imx6IpuInterruptController::WriteControl(uint32_t offset, uint32_t value) {
    control_[(offset - imx6_ipu::kOffIntCtrl0) >> 2] = value;
    UpdateLines();
}

void Imx6IpuInterruptController::Raise(uint32_t irq) {
    const uint32_t reg = irq / 32u;
    if (reg >= status_.size())
        emu_.Get<Fatal>().Die("[IPU] interrupt %u falls outside the 15 IPU_INT_STAT registers", irq);
    status_[reg] |= 1u << (irq & 31u);
    UpdateLines();
}

void Imx6IpuInterruptController::SaveState(StateWriter& writer) {
    writer.WriteBytes("status", status_.data(), sizeof(status_));
    writer.WriteBytes("control", control_.data(), sizeof(control_));
}

void Imx6IpuInterruptController::RestoreState(StateReader& reader) {
    reader.ReadBytes("status", status_.data(), sizeof(status_));
    reader.ReadBytes("control", control_.data(), sizeof(control_));
}

void Imx6IpuInterruptController::PostReset() {
    sync_irq_asserted_ = false;
    err_irq_asserted_ = false;
    UpdateLines();
}

void Imx6IpuInterruptController::PostRestore() {
    sync_irq_asserted_ = false;
    err_irq_asserted_ = false;
    UpdateLines();
}

void Imx6IpuInterruptController::UpdateLines() {
    static constexpr uint8_t kSyncRegs[] = {0, 1, 2, 3, 10, 11, 12, 13, 14};
    static constexpr uint8_t kErrRegs[] = {4, 5, 8, 9};
    const auto any_enabled_pending = [this](const uint8_t* registers, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            const uint32_t index = registers[i];
            if ((status_[index] & control_[index]) != 0u) return true;
        }
        return false;
    };
    const bool sync = any_enabled_pending(kSyncRegs, sizeof(kSyncRegs));
    const bool err = any_enabled_pending(kErrRegs, sizeof(kErrRegs));
    if (sync == sync_irq_asserted_ && err == err_irq_asserted_) return;
    auto& gic = emu_.Get<Imx6Gic>();
    if (sync != sync_irq_asserted_) {
        sync ? gic.AssertSpi(imx6_ipu::kIpuSyncSpi) : gic.DeAssertSpi(imx6_ipu::kIpuSyncSpi);
        sync_irq_asserted_ = sync;
    }
    if (err != err_irq_asserted_) {
        err ? gic.AssertSpi(imx6_ipu::kIpuErrSpi) : gic.DeAssertSpi(imx6_ipu::kIpuErrSpi);
        err_irq_asserted_ = err;
    }
}

REGISTER_SERVICE(Imx6IpuInterruptController);

}
