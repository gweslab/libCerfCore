#include "imx51_usboh3.h"

#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/core/log.h>
#include <cerfcore/cpu/emulated_memory.h>
#include "../../peripherals/usb/usb_device.h"
#include "../irq_controller.h"

namespace cerfcore {

namespace {

using Ci = ChipIdeaUsbCore;

/* EHCI 1.0 Spec Figure 3-7 (p46): Queue Head Structure Layout. */
constexpr uint32_t kQhEpCharOff       = 0x04u;
constexpr uint32_t kQhCurQtdOff       = 0x0Cu;
constexpr uint32_t kQhOverlayNextOff  = 0x10u;
constexpr uint32_t kQhOverlayTokenOff = 0x18u;
constexpr uint32_t kQhHorizLinkOff    = 0x00u;
constexpr uint32_t kQhTypMask  = 0x6u;
constexpr uint32_t kQhTypQh    = 0x2u;
constexpr uint32_t kQhHeadOfReclamation = 1u << 15;

/* EHCI 1.0 Spec Table 3-14/3-16 (p40-43): qTD structure and Token fields. */
constexpr uint32_t kQtdNextOff = 0x00u;
constexpr uint32_t kQtdTokenOff = 0x08u;
constexpr uint32_t kQtdBuf0Off  = 0x0Cu;
constexpr uint32_t kQtdTerminate = 1u;
constexpr uint32_t kQtdActive   = 0x80u;
constexpr uint32_t kQtdHalted   = 1u << 6;
constexpr uint32_t kQtdDataBufferError = 1u << 5;
constexpr uint32_t kQtdIoc      = 1u << 15;
constexpr uint32_t kQtdPidShift = 8u;
constexpr uint32_t kQtdPidMask  = 0x3u;
constexpr uint32_t kQtdPidOut   = 0u;
constexpr uint32_t kQtdPidIn    = 1u;
constexpr uint32_t kQtdPidSetup = 2u;
constexpr uint32_t kQtdTotalBytesShift = 16u;
constexpr uint32_t kQtdTotalBytesMask  = 0x7FFFu;
constexpr uint32_t kPageSizeLocal = 0x1000u;
constexpr int       kQtdChainGuard = 32;
constexpr int       kQhRingGuard   = 16;

/* EHCI 1.0 Spec Table 3-19 (p47): Endpoint Characteristics, QH DWord 1. */
uint32_t QhDeviceAddress(uint32_t ep_char) { return ep_char & 0x7Fu; }
uint32_t QhEndpointNumber(uint32_t ep_char) { return (ep_char >> 8) & 0xFu; }

}

void Imx51Usboh3::OnPortConnectChanged(int port_index) {
    if (port_index != 0) return;
    if (!cores_[0].ReportPortConnect(otg_host_root_port_.IsConnected())) return;
    RefreshDeviceIrq();
    UpdateScheduleTimer();
}

void Imx51Usboh3::WritePortsc(uint32_t core, uint32_t value) {
    const UsbDevice* attached = core == 0u ? otg_host_root_port_.Device() : nullptr;
    const ChipIdeaPortscWrite result = cores_[core].WritePortsc(value, attached);
    if (result == ChipIdeaPortscWrite::kRejected) {
        emu_.Get<Fatal>().Die("i.MX51 USB core %u PORTSC write 0x%08X over 0x%08X sets PIC, PTC, PFSC, STS, a "
                              "device-mode FPR, or SUSP on a disabled port, which CERF does not model", core,
                              value, cores_[core].Reg(Ci::kPortsc));
    }
    if (result == ChipIdeaPortscWrite::kDeviceReset) {
        if (UsbDevice* dev = otg_host_root_port_.Device()) dev->ResetToDefault();
    }
    RefreshDeviceIrq();
}

void Imx51Usboh3::ExecuteAsyncSchedule() {
    const uint32_t cmd = regs_[Ci::kUsbcmd >> 2];
    if (!(cmd & Ci::kUsbcmdRunStop)) return;

    if (!(cmd & Ci::kUsbcmdAse)) return;
    const uint32_t start = regs_[Ci::kListAddr >> 2];
    if (start == 0u) return;

    auto& mem = emu_.Get<EmulatedMemory>();
    ChipIdeaUsbCore& core = cores_[0];
    core.SetReclamation(true);
    uint32_t qh = start;
    for (int i = 0; i < kQhRingGuard; ++i) {
        if (!mem.TryTranslate(qh)) break;
        if ((mem.ReadWord(qh + kQhEpCharOff) & kQhHeadOfReclamation) != 0u) {
            if ((core.Usbsts() & Ci::kUsbstsReclaim) == 0u) break;
            core.SetReclamation(false);
        }
        if (ExecuteQueueHead(qh)) core.SetReclamation(true);
        const uint32_t link = mem.ReadWord(qh + kQhHorizLinkOff);
        if (link & kQtdTerminate) break;
        if ((link & kQhTypMask) != kQhTypQh) break;
        const uint32_t next_qh = link & ~0x1Fu;
        if (next_qh == start) break;
        qh = next_qh;
    }
}

void Imx51Usboh3::ExecutePeriodicSchedule() {
    const uint32_t cmd = regs_[Ci::kUsbcmd >> 2];
    if (!(cmd & Ci::kUsbcmdRunStop) || !(cmd & Ci::kUsbcmdPse)) return;

    const uint32_t base = regs_[Ci::kPeriodicList >> 2] & ~0xFFFu;
    if (base == 0u) return;

    const uint32_t elements = FreescaleUsbFrameIndex::FrameListElements(
        FreescaleUsbFrameIndex::FrameListSizeCode(cmd));
    const uint32_t index = (frame_index_.Frindex(0u) >> 3) & (elements - 1u);

    auto& mem = emu_.Get<EmulatedMemory>();
    uint32_t link = mem.ReadWord(base + index * 4u);
    for (int i = 0; i < kQhRingGuard && !(link & kQtdTerminate); ++i) {
        if ((link & kQhTypMask) != kQhTypQh) break;
        const uint32_t qh = link & ~0x1Fu;
        if (!mem.TryTranslate(qh)) break;
        ExecuteQueueHead(qh);
        link = mem.ReadWord(qh + kQhHorizLinkOff);
    }
}

bool Imx51Usboh3::ExecuteQueueHead(uint32_t qh_addr) {
    auto& mem = emu_.Get<EmulatedMemory>();
    /* EHCI 1.0 Figure 4-14: a halted queue advances horizontally. */
    if (mem.ReadWord(qh_addr + kQhOverlayTokenOff) & kQtdHalted) return false;
    const uint32_t ep_char = mem.ReadWord(qh_addr + kQhEpCharOff);
    const uint32_t dev_addr = QhDeviceAddress(ep_char);
    const uint32_t endpt    = QhEndpointNumber(ep_char);

    UsbDevice* root = otg_host_root_port_.Device();
    UsbDevice* dev  = root ? root->FindByAddress(static_cast<uint8_t>(dev_addr)) : nullptr;
    if (!dev) return false;

    uint32_t next_ptr = mem.ReadWord(qh_addr + kQhOverlayNextOff);
    uint32_t interrupts = 0;
    bool     executed   = false;
    for (int i = 0; i < kQtdChainGuard && !(next_ptr & kQtdTerminate); ++i) {
        const uint32_t qtd_addr = next_ptr & ~0x1Fu;
        if (!mem.TryTranslate(qtd_addr)) break;
        const uint32_t token = mem.ReadWord(qtd_addr + kQtdTokenOff);
        if (!(token & kQtdActive)) break;

        executed = true;
        const bool retired = ExecuteQtd(qtd_addr, dev, endpt);
        if (!retired) break;
        const uint32_t new_token = mem.ReadWord(qtd_addr + kQtdTokenOff);
        const bool halted = (new_token & kQtdHalted) != 0;
        const bool short_packet = !halted &&
            ((token >> kQtdPidShift) & kQtdPidMask) == kQtdPidIn &&
            ((new_token >> kQtdTotalBytesShift) & kQtdTotalBytesMask) != 0;
        /* EHCI 1.0 Table 2-10 and Table 3-15. */
        if (halted) interrupts |= Ci::kUsbstsUei;
        if ((token & kQtdIoc) || short_packet) interrupts |= Ci::kUsbstsUi;
        const uint32_t this_next = mem.ReadWord(qtd_addr + (short_packet ? 4u : kQtdNextOff));
        mem.WriteWord(qh_addr + kQhCurQtdOff, qtd_addr);
        mem.WriteWord(qh_addr + kQhOverlayNextOff, this_next);
        mem.WriteWord(qh_addr + kQhOverlayTokenOff, new_token);
        if (halted) break;
        next_ptr = this_next;
    }

    if (interrupts) {
        regs_[Ci::kUsbsts >> 2] |= interrupts;
        RefreshDeviceIrq();
    }
    return executed;
}

bool Imx51Usboh3::ExecuteQtd(uint32_t qtd_addr, UsbDevice* dev, uint32_t endpt) {
    auto& mem = emu_.Get<EmulatedMemory>();
    const uint32_t token = mem.ReadWord(qtd_addr + kQtdTokenOff);
    const uint32_t pid   = (token >> kQtdPidShift) & kQtdPidMask;
    const uint32_t total = (token >> kQtdTotalBytesShift) & kQtdTotalBytesMask;
    /* EHCI 1.0 Table 3-16: preserve token controls; STALL halts without consuming data. */
    auto retire = [&](uint32_t residual, bool halted, uint32_t error = 0u) {
        const auto next = (token & ~(kQtdActive | (kQtdTotalBytesMask << kQtdTotalBytesShift))) |
            (residual << kQtdTotalBytesShift) | (halted ? kQtdHalted : 0u) | error;
        mem.WriteWord(qtd_addr + kQtdTokenOff, next);
        return true;
    };
    if (pid != kQtdPidSetup && dev->IsEndpointStalled(static_cast<uint8_t>(endpt)))
        return retire(total, true);

    uint32_t pages[5];
    for (int p = 0; p < 5; ++p) pages[p] = mem.ReadWord(qtd_addr + kQtdBuf0Off + p * 4u);

    /* EHCI 1.0 Table 3-17 and 4.10.6: page-zero low bits offset C_Page. */
    const uint32_t current_page = (token >> 12) & 7u;
    const uint32_t offset = pages[0] & (kPageSizeLocal - 1u);
    if (current_page >= 5 || total > (5u - current_page) * kPageSizeLocal - offset) {
        LOG(Caution, "USB: invalid qTD buffer span qtd=%08X page=%u offset=%u bytes=%u\n",
            qtd_addr, current_page, offset, total);
        return retire(total, true, kQtdDataBufferError);
    }
    auto transfer = [&](uint8_t* data, uint32_t n, bool to_guest) {
        uint32_t left = n, cursor = 0u;
        for (uint32_t p = current_page; p < 5 && left; ++p) {
            const uint32_t start = p == current_page ? offset : 0u;
            const uint32_t pa = (pages[p] & ~(kPageSizeLocal - 1u)) + start;
            const uint32_t inpg = kPageSizeLocal - start;
            const uint32_t chunk = inpg < left ? inpg : left;
            if (to_guest) mem.CopyIn(pa, data + cursor, chunk);
            else mem.CopyOut(pa, data + cursor, chunk);
            cursor += chunk;
            left   -= chunk;
        }
    };

    uint32_t residual = 0u;

    if (endpt == 0u && pid == kQtdPidSetup) {
        if (total != UsbDevice::SetupPacket::kSize) return retire(total, true);
        uint8_t raw[UsbDevice::SetupPacket::kSize] = {};
        transfer(raw, UsbDevice::SetupPacket::kSize, false);
        const UsbDevice::SetupPacket setup = UsbDevice::SetupPacket::Decode(raw);
        if (!dev->BeginControlTransfer(setup)) return retire(total, true);
        residual = 0u;
    } else if (endpt == 0u && pid == kQtdPidIn) {
        std::vector<uint8_t> data(total);
        const uint32_t n = dev->ReadControlReply(data.data(), total);
        if (n > 0u) transfer(data.data(), n, true);
        residual = total - n;
    } else if (endpt == 0u && pid == kQtdPidOut) {
        if (total != 0u) {
            LOG(Caution, "USB: unsupported control OUT data length=%u\n", total);
            return retire(total, true);
        }
        dev->FinishControlTransfer();
        residual = 0u;
    } else if (pid == kQtdPidOut) {
        std::vector<uint8_t> data(total);
        transfer(data.data(), total, false);
        dev->OnBulkOut(static_cast<uint8_t>(endpt), data.data(), total);
        if (dev->IsEndpointStalled(static_cast<uint8_t>(endpt))) return retire(total, true);
        residual = 0u;
    } else if (pid == kQtdPidIn) {
        std::vector<uint8_t> data(total);
        const uint32_t got = dev->OnBulkIn(static_cast<uint8_t>(endpt), data.data(), total);
        if (got == UsbDevice::kNak) return false;
        if (got == 0u && dev->IsEndpointStalled(static_cast<uint8_t>(endpt)))
            return retire(total, true);
        if (got > 0u) transfer(data.data(), got, true);
        residual = total - got;
    } else {
        return retire(total, true);
    }

    return retire(residual, false);
}

}
