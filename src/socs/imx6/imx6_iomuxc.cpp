#include "imx6_iomuxc.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/state_stream.h>
#include <cerfcore/boards/board_context.h>
#include "../../peripherals/peripheral_dispatcher.h"
#include <cerfcore/socs/guest_cpu_reset.h>
#include "imx6_gpio_bus.h"
#include "imx6_gpio_pads.h"
#include "imx6_mmio_lane.h"
#include "imx6_id.h"

namespace cerfcore {

namespace {

constexpr Imx6WdogPadMux kWdogPadMuxes[] = {
    {"DISP0_DAT8", Imx6WdogPadSignal::WdogB, 4u, 29u, 3u, 1u},
    {"GPIO_9", Imx6WdogPadSignal::WdogB, 1u, 9u, 1u, 1u},
    {"SD1_DAT2", Imx6WdogPadSignal::WdogB, 1u, 19u, 4u, 1u},
    {"SD1_DAT2", Imx6WdogPadSignal::ResetBDeb, 1u, 19u, 6u, 1u},
    {"DISP0_DAT9", Imx6WdogPadSignal::WdogB, 4u, 30u, 3u, 2u},
    {"GPIO_1", Imx6WdogPadSignal::WdogB, 1u, 1u, 1u, 2u},
    {"SD1_DAT3", Imx6WdogPadSignal::WdogB, 1u, 21u, 4u, 2u},
    {"SD1_DAT3", Imx6WdogPadSignal::ResetBDeb, 1u, 21u, 6u, 2u}
};

}

bool Imx6Iomuxc::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && SocId::IsImx6(bd->GetSocId());
}

void Imx6Iomuxc::OnReady() {
    family_ = emu_.Get<Imx6Variant>().Family();
    Reset();
    emu_.Get<PeripheralDispatcher>().Register(this);
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        if (emu_.Get<GuestCpuReset>().DeliveredResetWasResume()) return;
        Reset();
    });
}

void Imx6Iomuxc::Reset() {
    std::lock_guard<std::recursive_mutex> lk(emu_.Get<Imx6GpioBus>().StateMutex());
    ResetRegisters();
}

void Imx6Iomuxc::ResetRegisters() {
    constexpr uint32_t gpr_resets[] = {
        0x00000000u, 0x48400005u, 0x00000000u, 0x01E00000u,
        0x00000000u, 0x00000000u, 0x22222222u, 0x22222222u,
        0x00000000u, 0x00000000u, 0x00003800u, 0u,
        0x0F000000u, 0u
    };
    const Imx6Variant& variant = emu_.Get<Imx6Variant>();
    for (uint32_t i = 0; i < 14u; ++i) {
        regs_[i] = gpr_resets[i];
        known_.set(i);
    }
    regs_[11] = variant.IomuxcGpr11Reset();
    regs_[13] = variant.IomuxcGpr13Reset();
    const Imx6GpioPadTable& pads = Imx6GpioPadsFor(family_);
    for (size_t i = 0; i < pads.count; ++i) {
        regs_[pads.pads[i].mux_offset >> 2] = pads.pads[i].mux_reset;
        regs_[pads.pads[i].ctl_offset >> 2] = pads.pads[i].ctl_reset;
        known_.set(pads.pads[i].mux_offset >> 2);
        known_.set(pads.pads[i].ctl_offset >> 2);
    }
}

uint8_t Imx6Iomuxc::ReadByte(uint32_t addr) {
    return Imx6ReadMmioByte(addr, [this](uint32_t a) { return ReadWord(a); });
}

uint16_t Imx6Iomuxc::ReadHalf(uint32_t addr) {
    return Imx6ReadMmioHalf(addr, [this](uint32_t a) { return ReadWord(a); });
}

uint32_t Imx6Iomuxc::ReadWord(uint32_t addr) {
    std::lock_guard<std::recursive_mutex> lk(emu_.Get<Imx6GpioBus>().StateMutex());
    const uint32_t off = addr - MmioBase();
    if (!IsRegister(off) || !known_.test(off >> 2))
        HaltUnsupportedAccess("read32 of an IOMUXC register whose reset value is not modelled", addr, 0);
    return regs_[off >> 2];
}

void Imx6Iomuxc::WriteByte(uint32_t addr, uint8_t value) {
    Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Byte, [this](const Imx6MmioLane& lane) {
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    });
}
void Imx6Iomuxc::WriteHalf(uint32_t addr, uint16_t value) {
    Imx6ForEachMmioLane(addr, value, Imx6MmioWidth::Half, [this](const Imx6MmioLane& lane) {
        WriteWord(lane.address, lane.Merge(ReadWord(lane.address)));
    });
}

void Imx6Iomuxc::CheckGprWrite(uint32_t addr, uint32_t off, uint32_t value) {
    const uint32_t changed = value ^ regs_[off >> 2];
    if (off == kGpr1) {
        if (value == 0x4840201Bu || value == 0x4860201Bu) return;
        if (value != 0x48400005u && value != 0x4840001Bu && value != 0x4860001Bu)
            HaltUnsupportedAccess("IOMUXC GPR1 value has no guest writer", addr, value);
        return;
    }
    if (off == kGpr2) {
        if ((value & ~kGpr2Modelled) != 0u)
            HaltUnsupportedAccess("IOMUXC GPR2 split mode or LDB counter reset is not modelled", addr, value);
        return;
    }
    if (changed != 0u) HaltUnsupportedAccess("IOMUXC GPR write with no modelled effect", addr, value);
}

void Imx6Iomuxc::WriteWord(uint32_t addr, uint32_t value) {
    std::lock_guard<std::recursive_mutex> lk(emu_.Get<Imx6GpioBus>().StateMutex());
    const uint32_t off = addr - MmioBase();
    if (!IsRegister(off)) HaltUnsupportedAccess("write32", addr, value);
    if (off < kGprEnd) CheckGprWrite(addr, off, value);
    const Imx6GpioPadTable& pads = Imx6GpioPadsFor(family_);
    uint32_t stored_value = value;
    for (size_t i = 0; i < pads.count; ++i) {
        const Imx6GpioPad& pad = pads.pads[i];
        if (pad.mux_offset == off) stored_value = value & (kImx6PadMuxMode | kPadMuxSion);
        if (pad.mux_offset == off || pad.ctl_offset == off)
            emu_.Get<Imx6GpioBus>().BeforePadChange(pad.bank, pad.pin);
    }
    regs_[off >> 2] = stored_value;
    known_.set(off >> 2);
    for (size_t i = 0; i < pads.count; ++i) {
        const Imx6GpioPad& pad = pads.pads[i];
        if (pad.mux_offset == off || pad.ctl_offset == off)
            emu_.Get<Imx6GpioBus>().AfterPadChange(pad.bank, pad.pin);
    }
}

void Imx6Iomuxc::SaveState(StateWriter& w) {
    std::lock_guard<std::recursive_mutex> lk(emu_.Get<Imx6GpioBus>().StateMutex());
    w.WriteBytes("regs", regs_, sizeof(regs_));
    uint8_t known[kWords / 8u]{};
    for (uint32_t i = 0; i < kWords; ++i)
        if (known_.test(i)) known[i / 8u] |= static_cast<uint8_t>(1u << (i % 8u));
    w.WriteBytes("known", known, sizeof(known));
}

void Imx6Iomuxc::RestoreState(StateReader& r) {
    std::lock_guard<std::recursive_mutex> lk(emu_.Get<Imx6GpioBus>().StateMutex());
    r.ReadBytes("regs", regs_, sizeof(regs_));
    uint8_t known[kWords / 8u]{};
    r.ReadBytes("known", known, sizeof(known));
    known_.reset();
    for (uint32_t i = 0; i < kWords; ++i)
        if ((known[i / 8u] >> (i % 8u)) & 1u) known_.set(i);
}

bool Imx6Iomuxc::IsRegister(uint32_t off) const {
    if ((off & 3u) != 0u) return false;
    if (off <= 0x034u) return true;
    return emu_.Get<Imx6Variant>().IomuxcIsRegister(off);
}

uint32_t Imx6Iomuxc::PadRegister(uint32_t off) const {
    std::lock_guard<std::recursive_mutex> lk(emu_.Get<Imx6GpioBus>().StateMutex());
    return regs_[off >> 2];
}

const Imx6WdogPadMux* Imx6Iomuxc::WdogPadMux(uint32_t wdog, Imx6WdogPadSignal signal) const {
    const Imx6GpioPadTable& table = Imx6GpioPadsFor(family_);
    for (const Imx6WdogPadMux& pad : kWdogPadMuxes) {
        if (pad.unit != wdog || pad.signal != signal) continue;
        const Imx6GpioPad* gpio_pad = FindImx6GpioPad(table, pad.bank, pad.pin);
        if (gpio_pad && (PadRegister(gpio_pad->mux_offset) & kImx6PadMuxMode) == pad.mux_mode) return &pad;
    }
    return nullptr;
}

REGISTER_SERVICE(Imx6Iomuxc);

}
