#pragma once

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include <cerfcore/peripherals/peripheral_base.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include "imx6_mmio_lane.h"
#include "imx6_id.h"

#include <algorithm>

namespace cerfcore {

namespace cerf_imx6_usb_phy_detail {

template <uint32_t kBase> class Imx6UsbPhy : public Peripheral {
public:
    using Peripheral::Peripheral;

    bool ShouldRegister() override {
        auto* bd = emu_.TryGet<BoardContext>();
        return bd && SocId::IsImx6(bd->GetSocId());
    }

    void OnReady() override {
        Reset();
        emu_.Get<PeripheralDispatcher>().Register(this);
        emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) { Reset(); });
    }

    uint32_t MmioBase() const override { return kBase; }

private:
    void Reset() {
        std::fill(std::begin(regs_), std::end(regs_), 0u);
        ResetPhyRegisters();
        regs_[kRegStatus >> 4] = 0x00000000u;
        regs_[kRegDebug >> 4] = 0x7F180000u;
        regs_[kRegDebug0Status >> 4] = 0x00000000u;
        regs_[kRegDebug1 >> 4] = 0x00001000u;
    }

    uint32_t MmioSize() const override { return 0x1000u; }

    uint8_t ReadByte(uint32_t addr) override {
        return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint16_t ReadHalf(uint32_t addr) override {
        return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
    }
    uint32_t ReadWord(uint32_t addr) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usbphy read32 unaligned", addr, 0);
        if (!IsModelledRegister(off)) HaltUnsupportedAccess("imx6-usbphy read32 unmodelled register", addr, 0);
        if (off == kRegStatus) HaltUnsupportedAccess("imx6-usbphy read of the USBPHY STATUS detect results", addr, 0);

        return regs_[(off & ~0xFu) >> 4];
    }
    void WriteByte(uint32_t addr, uint8_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteHalf(uint32_t addr, uint16_t value) override {
        Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half,
                            [this](const Imx6MmioLane& lane) { WriteLane(lane); });
    }
    void WriteWord(uint32_t addr, uint32_t value) override {
        const uint32_t off = addr - MmioBase();
        if ((off & 3u) != 0) HaltUnsupportedAccess("imx6-usbphy write32 unaligned", addr, value);
        if (!IsModelledRegister(off)) HaltUnsupportedAccess("imx6-usbphy write32 unmodelled register", addr, value);

        const uint32_t reg = off & ~0xFu;
        if (reg == kRegStatus)
            HaltUnsupportedAccess("imx6-usbphy write to USBPHY STATUS", addr, value);
        if (reg == kRegDebug0Status)
            HaltUnsupportedAccess("imx6-usbphy write to the read-only DEBUG0_STATUS", addr, value);
        uint32_t& slot = regs_[reg >> 4];
        ApplyRegisterWrite(slot, off & 0xCu, value);
        if (reg == kRegCtrl && (slot & kCtrlSftrst) != 0u) ResetPhyRegisters();
    }

    void SaveState(StateWriter& w) override { w.WriteBytes("regs", regs_, sizeof(regs_)); }

    void RestoreState(StateReader& r) override { r.ReadBytes("regs", regs_, sizeof(regs_)); }

private:
    static constexpr uint32_t kRegPwd = 0x00u;
    static constexpr uint32_t kRegTx = 0x10u;
    static constexpr uint32_t kRegRx = 0x20u;
    static constexpr uint32_t kRegCtrl = 0x30u;
    static constexpr uint32_t kRegStatus = 0x40u;
    static constexpr uint32_t kRegDebug = 0x50u;
    static constexpr uint32_t kRegDebug0Status = 0x60u;
    static constexpr uint32_t kRegDebug1 = 0x70u;
    static constexpr uint32_t kCtrlSftrst = 1u << 31;

    static void ApplyRegisterWrite(uint32_t& slot, uint32_t operation, uint32_t value) {
        switch (operation) {
        case 0x0u: slot = value; break;
        case 0x4u: slot |= value; break;
        case 0x8u: slot &= ~value; break;
        case 0xCu: slot ^= value; break;
        }
    }

    void ResetPhyRegisters() {
        regs_[kRegPwd >> 4] = 0x001E1C00u;
        regs_[kRegTx >> 4] = 0x10060607u;
        regs_[kRegRx >> 4] = 0x00000000u;
        regs_[kRegCtrl >> 4] = 0xC0200000u;
    }

    static bool IsModelledRegister(uint32_t off) {
        if ((off & 3u) != 0) return false;
        const uint32_t reg = off & ~0xFu;
        const bool aliases = reg == kRegPwd || reg == kRegTx || reg == kRegRx || reg == kRegCtrl ||
                             reg == kRegDebug || reg == kRegDebug1;
        return aliases || off == kRegStatus || off == kRegDebug0Status;
    }

    void WriteLane(const Imx6MmioLane& lane) {
        const uint32_t off = lane.address - MmioBase();
        if (!IsModelledRegister(off))
            HaltUnsupportedAccess("imx6-usbphy write lane unmodelled register",
                                  lane.address, lane.value);
        const uint32_t reg = off & ~0xFu;
        if (reg == kRegStatus)
            HaltUnsupportedAccess("imx6-usbphy write to USBPHY STATUS", lane.address, lane.value);
        if (reg == kRegDebug0Status)
            HaltUnsupportedAccess("imx6-usbphy write to the read-only DEBUG0_STATUS", lane.address, lane.value);
        uint32_t& slot = regs_[reg >> 4];
        ApplyRegisterWrite(slot, off & 0xCu, (off & 0xCu) == 0u ? lane.Merge(slot) : lane.value);
        if (reg == kRegCtrl && (slot & kCtrlSftrst) != 0u) ResetPhyRegisters();
    }

    uint32_t regs_[0x80u / 0x10u]{};
};

}

using cerf_imx6_usb_phy_detail::Imx6UsbPhy;

}
