#include "nec_vrc4171a_lcd.h"

#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/socs/guest_cpu_reset.h>
#include <cerfcore/state/state_stream.h>
#include "../peripheral_dispatcher.h"
#include "nec_vrc4171a.h"
#include "nec_vrc4171a_frame_memory.h"
#include "nec_vrc4171a_lcd_board.h"

#include <algorithm>

namespace cerfcore {

namespace {

constexpr uint32_t kRegisterBytes = 0x60u;

constexpr uint32_t kRegPanelSelect       = 0x00u;
constexpr uint32_t kRegPanelControl      = 0x02u;
constexpr uint32_t kRegPowerMode         = 0x04u;
constexpr uint32_t kRegVmclkEnable       = 0x06u;
constexpr uint32_t kRegVclkEnable        = 0x08u;
constexpr uint32_t kRegFifoMemory        = 0x0Au;
constexpr uint32_t kRegVerticalHalf      = 0x0Cu;
constexpr uint32_t kRegHorizontalDisplay = 0x10u;
constexpr uint32_t kRegHorizontalRetrace = 0x12u;
constexpr uint32_t kRegVerticalEnd       = 0x14u;
constexpr uint32_t kRegVerticalTotal     = 0x16u;
constexpr uint32_t kRegVerticalRetrace   = 0x18u;
constexpr uint32_t kRegVerticalRetraceEnd = 0x1Au;
constexpr uint32_t kRegStartAddress      = 0x1Cu;
constexpr uint32_t kRegOffset            = 0x1Eu;
constexpr uint32_t kRegCursorControl     = 0x20u;
constexpr uint32_t kRegCursorX           = 0x22u;
constexpr uint32_t kRegCursorY           = 0x24u;
constexpr uint32_t kRegCursorColor0A     = 0x26u;
constexpr uint32_t kRegCursorColor0B     = 0x28u;
constexpr uint32_t kRegCursorColor1A     = 0x2Au;
constexpr uint32_t kRegCursorColor1B     = 0x2Cu;
constexpr uint32_t kRegCursorOrigin      = 0x2Eu;
constexpr uint32_t kRegRamWriteAddress   = 0x40u;
constexpr uint32_t kRegRamWritePort0     = 0x42u;
constexpr uint32_t kRegRamWritePort1     = 0x44u;
constexpr uint32_t kRegRamReadAddress    = 0x46u;
constexpr uint32_t kRegRamReadPort0      = 0x48u;
constexpr uint32_t kRegRamReadPort1      = 0x4Au;
constexpr uint32_t kRegScratch0          = 0x50u;
constexpr uint32_t kRegScratch1          = 0x52u;

constexpr uint16_t kReservedPanelSelect  = 0xFFB0u;
constexpr uint16_t kReservedPanelControl = 0xF0F8u;
constexpr uint16_t kReservedPowerMode    = 0xFFC0u;
constexpr uint16_t kReservedClockEnable  = 0xFFFEu;
constexpr uint16_t kReservedFifoMemory   = 0xE004u;
constexpr uint16_t kReservedVerticalHalf = 0x1800u;
constexpr uint16_t kReservedVertical     = 0xF800u;
constexpr uint16_t kReservedRetraceEnd   = 0xFC00u;
constexpr uint16_t kReservedOffset       = 0x8C00u;
constexpr uint16_t kReservedCursorControl = 0xFFFAu;
constexpr uint16_t kReservedCursorPosition = 0xF800u;
constexpr uint16_t kReservedRedGreen     = 0xC0C0u;
constexpr uint16_t kReservedBlue         = 0xFFC0u;
constexpr uint16_t kReservedRamAddress   = 0xFF00u;

constexpr uint16_t kPanelControlWave     = 0x0800u;
constexpr uint16_t kPanelControlSignals  = 0x0700u;
constexpr uint16_t kPanelControlDepth    = 0x0007u;
constexpr uint16_t kPowerEnVee           = 0x0001u;
constexpr uint16_t kPowerEnPanel         = 0x0002u;
constexpr uint16_t kPowerEnVdd           = 0x0004u;
constexpr uint16_t kPowerBacklight       = 0x0008u;
constexpr uint16_t kPowerDown            = 0x0010u;
constexpr uint16_t kPowerSelfRefresh     = 0x0020u;
constexpr uint16_t kClockEnable          = 0x0001u;
constexpr uint16_t kFifoScreenOff        = 0x0200u;
constexpr uint16_t kFifoDramOff          = 0x0100u;
constexpr uint16_t kFifoMemConf          = 0x0018u;
constexpr uint16_t kFifoMemConfInvalid   = 0x0018u;
constexpr uint16_t kFifoMemConfHalfMb    = 0x0000u;
constexpr uint16_t kFifoMemConfOneMb     = 0x0010u;
constexpr uint32_t kHalfMbBytes          = 0x00080000u;
constexpr uint32_t kOneMbBytes           = 0x00100000u;
constexpr uint32_t kTwoMbBytes           = 0x00200000u;
constexpr uint16_t kCursorEnable         = 0x0001u;
constexpr uint16_t kCursorPattern        = 0x0004u;
constexpr uint32_t kCursorAreaBytes      = 0x200u;
constexpr uint32_t kCursorPatternBytes   = 0x100u;
constexpr uint16_t kCursorOriginMask     = 0x003Fu;
constexpr uint16_t kCursorOriginYShift   = 8u;
constexpr uint16_t kHorizontalEndShift   = 8u;
constexpr uint16_t kVerticalCount        = 0x07FFu;
constexpr uint16_t kOffsetOverflowShift  = 12u;
constexpr uint16_t kOffsetOverflowMask   = 0x0007u;
constexpr uint16_t kOffsetScreenWidth    = 0x03FFu;
constexpr uint32_t kStartOverflowShift   = 18u;
constexpr uint32_t kStartAddressShift    = 2u;
constexpr uint32_t kCharacterPixels      = 8u;
constexpr uint32_t kScreenWidthUnit      = 8u;
constexpr uint16_t kColorMask            = 0x3Fu;
constexpr uint16_t kRedShift             = 8u;

}

REGISTER_SERVICE(NecVrc4171aLcd);

bool NecVrc4171aLcd::ShouldRegister() {
    return emu_.TryGet<NecVrc4171aLcdBoard>() != nullptr;
}

void NecVrc4171aLcd::OnReady() {
    auto& board    = emu_.Get<NecVrc4171aLcdBoard>();
    register_base_   = board.RegisterBase();
    panel_interface_ = board.PanelInterface();
    frame_           = &emu_.Get<NecVrc4171aFrameMemory>();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        ResetRegisters();
        ApplyMemoryDecodeLocked();
    }
    emu_.Get<GuestCpuReset>().RegisterResetListener([this](ResetLineKind) {
        std::lock_guard<std::mutex> lk(mtx_);
        ResetRegisters();
        ApplyMemoryDecodeLocked();
        size_latch_.Reset();
    });
    emu_.Get<PeripheralDispatcher>().Register(this);
}

uint32_t NecVrc4171aLcd::MmioSize() const { return kRegisterBytes; }

void NecVrc4171aLcd::ResetRegisters() {
    panel_select_ = panel_control_ = power_mode_ = fifo_memory_ = vertical_half_ = 0u;
    vmclk_enable_ = vclk_enable_ = kClockEnable;
    h_display_ = h_retrace_ = v_end_ = v_total_ = v_retrace_start_ = v_retrace_end_ = 0u;
    start_address_ = offset_ = 0u;
    cursor_control_ = cursor_x_ = cursor_y_ = cursor_origin_ = 0u;
    cursor_color0a_ = cursor_color0b_ = cursor_color1a_ = cursor_color1b_ = 0u;
    ram_write_addr_ = ram_read_addr_ = 0u;
    scratch_[0] = scratch_[1] = 0u;
}

void NecVrc4171aLcd::CheckReserved(uint32_t off, uint16_t value, uint16_t reserved) {
    if ((value & reserved) != 0u)
        emu_.Get<Fatal>().Die("[VRC4171A LCD] write 0x%04X to register 0x%02X sets reserved bits 0x%04X",
                              value, off, value & reserved);
}

void NecVrc4171aLcd::CheckPanelWiring(uint32_t off, uint16_t value, uint16_t expected) {
    if (value != expected)
        emu_.Get<Fatal>().Die("[VRC4171A LCD] register 0x%02X value 0x%04X does not drive the board's panel, which "
                              "takes 0x%04X", off, value, expected);
}

void NecVrc4171aLcd::CheckPanelInterfaceAtEnable(uint16_t power_mode) {
    if (panel_select_ != panel_interface_.panel_select ||
        (panel_control_ & kPanelControlSignals) != panel_interface_.panel_signals ||
        vertical_half_ != panel_interface_.vertical_half)
        emu_.Get<Fatal>().Die("[VRC4171A LCD] Power Mode 0x%04X enables the panel with Panel Select 0x%04X, Panel "
                              "Control 0x%04X and Vertical Half 0x%04X, which do not drive the board's panel",
                              power_mode, panel_select_, panel_control_, vertical_half_);
}

uint32_t NecVrc4171aLcd::ConfiguredMemoryBytes() const {
    switch (fifo_memory_ & kFifoMemConf) {
        case kFifoMemConfHalfMb: return std::min(kHalfMbBytes, frame_->MmioSize());
        case kFifoMemConfOneMb:  return std::min(kOneMbBytes, frame_->MmioSize());
        default:                 return std::min(kTwoMbBytes, frame_->MmioSize());
    }
}

void NecVrc4171aLcd::ApplyMemoryDecodeLocked() { frame_->SetDecodedBytes(ConfiguredMemoryBytes()); }

uint16_t NecVrc4171aLcd::ReadHalf(uint32_t addr) {
    const uint32_t off = addr - register_base_;
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kRegPanelSelect:       return panel_select_;
        case kRegPanelControl:      return panel_control_;
        case kRegPowerMode:         return power_mode_;
        case kRegVmclkEnable:       return vmclk_enable_;
        case kRegVclkEnable:        return vclk_enable_;
        case kRegFifoMemory:        return fifo_memory_;
        case kRegVerticalHalf:      return vertical_half_;
        case kRegHorizontalDisplay: return h_display_;
        case kRegHorizontalRetrace: return h_retrace_;
        case kRegVerticalEnd:       return v_end_;
        case kRegVerticalTotal:     return v_total_;
        case kRegVerticalRetrace:   return v_retrace_start_;
        case kRegVerticalRetraceEnd: return v_retrace_end_;
        case kRegStartAddress:      return start_address_;
        case kRegOffset:            return offset_;
        case kRegCursorControl:     return cursor_control_;
        case kRegCursorX:           return cursor_x_;
        case kRegCursorY:           return cursor_y_;
        case kRegCursorColor0A:     return cursor_color0a_;
        case kRegCursorColor0B:     return cursor_color0b_;
        case kRegCursorColor1A:     return cursor_color1a_;
        case kRegCursorColor1B:     return cursor_color1b_;
        case kRegCursorOrigin:      return cursor_origin_;
        case kRegRamReadPort0: {
            const uint32_t entry = palette_[ram_read_addr_];
            return static_cast<uint16_t>((PaletteRed(entry) << kRedShift) | PaletteGreen(entry));
        }
        case kRegRamReadPort1:      return static_cast<uint16_t>(PaletteBlue(palette_[ram_read_addr_]));
        case kRegScratch0:          return scratch_[0];
        case kRegScratch1:          return scratch_[1];
        default:
            HaltUnsupportedAccess("VRC4171A LCD register read", addr, 0);
    }
}

void NecVrc4171aLcd::WriteHalf(uint32_t addr, uint16_t value) {
    const uint32_t off = addr - register_base_;
    std::lock_guard<std::mutex> lk(mtx_);
    switch (off) {
        case kRegPanelSelect:
            CheckReserved(off, value, kReservedPanelSelect);
            CheckPanelWiring(off, value, panel_interface_.panel_select);
            panel_select_ = value;
            return;
        case kRegPanelControl:
            CheckReserved(off, value, kReservedPanelControl);
            if ((value & kPanelControlWave) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A LCD] Panel Control 0x%04X enables the wave equation", value);
            CheckPanelWiring(off, value & kPanelControlSignals, panel_interface_.panel_signals);
            panel_control_ = value;
            return;
        case kRegPowerMode:
            CheckReserved(off, value, kReservedPowerMode);
            if ((value & (kPowerDown | kPowerSelfRefresh)) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A LCD] Power Mode 0x%04X enables power down or DRAM self refresh",
                                      value);
            if ((value & kPowerEnPanel) != 0u) CheckPanelInterfaceAtEnable(value);
            power_mode_ = value;
            return;
        case kRegVmclkEnable:
            CheckReserved(off, value, kReservedClockEnable);
            vmclk_enable_ = value;
            return;
        case kRegVclkEnable:
            CheckReserved(off, value, kReservedClockEnable);
            vclk_enable_ = value;
            return;
        case kRegFifoMemory:
            CheckReserved(off, value, kReservedFifoMemory);
            if ((value & kFifoMemConf) == kFifoMemConfInvalid)
                emu_.Get<Fatal>().Die("[VRC4171A LCD] FIFO/memory write 0x%04X selects the invalid MEM_CONF 11",
                                      value);
            if ((value & kFifoDramOff) != 0u)
                emu_.Get<Fatal>().Die("[VRC4171A LCD] FIFO/memory write 0x%04X tri-states the DRAM interface",
                                      value);
            fifo_memory_ = value;
            ApplyMemoryDecodeLocked();
            return;
        case kRegVerticalHalf:
            CheckReserved(off, value, kReservedVerticalHalf);
            CheckPanelWiring(off, value, panel_interface_.vertical_half);
            vertical_half_ = value;
            return;
        case kRegHorizontalDisplay: h_display_ = value; return;
        case kRegHorizontalRetrace: h_retrace_ = value; return;
        case kRegVerticalEnd:
            CheckReserved(off, value, kReservedVertical);
            v_end_ = value;
            return;
        case kRegVerticalTotal:
            CheckReserved(off, value, kReservedVertical);
            v_total_ = value;
            return;
        case kRegVerticalRetrace:
            CheckReserved(off, value, kReservedVertical);
            v_retrace_start_ = value;
            return;
        case kRegVerticalRetraceEnd:
            CheckReserved(off, value, kReservedRetraceEnd);
            v_retrace_end_ = value;
            return;
        case kRegStartAddress: start_address_ = value; return;
        case kRegOffset:
            CheckReserved(off, value, kReservedOffset);
            offset_ = value;
            return;
        case kRegCursorControl:
            CheckReserved(off, value, kReservedCursorControl);
            cursor_control_ = value;
            return;
        case kRegCursorX:
            CheckReserved(off, value, kReservedCursorPosition);
            cursor_x_ = value;
            return;
        case kRegCursorY:
            CheckReserved(off, value, kReservedCursorPosition);
            cursor_y_ = value;
            return;
        case kRegCursorColor0A:
            CheckReserved(off, value, kReservedRedGreen);
            cursor_color0a_ = value;
            return;
        case kRegCursorColor0B:
            CheckReserved(off, value, kReservedBlue);
            cursor_color0b_ = value;
            return;
        case kRegCursorColor1A:
            CheckReserved(off, value, kReservedRedGreen);
            cursor_color1a_ = value;
            return;
        case kRegCursorColor1B:
            CheckReserved(off, value, kReservedBlue);
            cursor_color1b_ = value;
            return;
        case kRegCursorOrigin:
            CheckReserved(off, value, kReservedRedGreen);
            cursor_origin_ = value;
            return;
        case kRegRamWriteAddress:
            CheckReserved(off, value, kReservedRamAddress);
            ram_write_addr_ = value;
            return;
        case kRegRamWritePort0: {
            CheckReserved(off, value, kReservedRedGreen);
            uint32_t& entry = palette_[ram_write_addr_];
            entry = PaletteEntry((value >> kRedShift) & kColorMask, value & kColorMask, PaletteBlue(entry));
            return;
        }
        case kRegRamWritePort1: {
            CheckReserved(off, value, kReservedBlue);
            uint32_t& entry = palette_[ram_write_addr_];
            entry = PaletteEntry(PaletteRed(entry), PaletteGreen(entry), value & kColorMask);
            return;
        }
        case kRegRamReadAddress:
            CheckReserved(off, value, kReservedRamAddress);
            ram_read_addr_ = value;
            return;
        case kRegScratch0: scratch_[0] = value; return;
        case kRegScratch1: scratch_[1] = value; return;
        default:
            HaltUnsupportedAccess("VRC4171A LCD register write", addr, value);
    }
}

void NecVrc4171aLcd::Scan(NecVrc4171aLcdScan& out) {
    const bool clocks = emu_.Get<NecVrc4171a>().DisplayClocksEnabled();
    std::lock_guard<std::mutex> lk(mtx_);
    out.display_on = clocks && (power_mode_ & kPowerEnPanel) != 0u &&
                     (vmclk_enable_ & kClockEnable) != 0u && (vclk_enable_ & kClockEnable) != 0u &&
                     (fifo_memory_ & kFifoScreenOff) == 0u;
    out.vdd_on      = (power_mode_ & kPowerEnVdd) != 0u;
    out.color_depth = panel_control_ & kPanelControlDepth;
    out.width  = (static_cast<uint32_t>(h_display_ >> kHorizontalEndShift) + 1u) * kCharacterPixels;
    out.height = static_cast<uint32_t>(v_end_ & kVerticalCount) + 1u;
    out.start  = (static_cast<uint32_t>((offset_ >> kOffsetOverflowShift) & kOffsetOverflowMask)
                  << kStartOverflowShift) |
                 (static_cast<uint32_t>(start_address_) << kStartAddressShift);
    out.stride       = static_cast<uint32_t>(offset_ & kOffsetScreenWidth) * kScreenWidthUnit;
    out.memory_bytes = ConfiguredMemoryBytes();
    out.cursor_on    = (cursor_control_ & kCursorEnable) != 0u;
    out.cursor_base  = out.memory_bytes - kCursorAreaBytes +
                       ((cursor_control_ & kCursorPattern) != 0u ? kCursorPatternBytes : 0u);
    out.cursor_left  = static_cast<int32_t>(cursor_x_) - static_cast<int32_t>(cursor_origin_ & kCursorOriginMask);
    out.cursor_top   = static_cast<int32_t>(cursor_y_) -
                       static_cast<int32_t>((cursor_origin_ >> kCursorOriginYShift) & kCursorOriginMask);
    out.cursor_color0a = cursor_color0a_;
    out.cursor_color0b = cursor_color0b_;
    out.cursor_color1a = cursor_color1a_;
    out.cursor_color1b = cursor_color1b_;
    out.palette        = palette_;
}

bool NecVrc4171aLcd::BacklightOn() {
    std::lock_guard<std::mutex> lk(mtx_);
    return (power_mode_ & kPowerBacklight) != 0u && (power_mode_ & kPowerEnVee) != 0u;
}

void NecVrc4171aLcd::PublishPanelEnable(bool visible) {
    size_latch_.PublishOnce(emu_, visible);
}

void NecVrc4171aLcd::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.Write("panel_select", panel_select_);
    w.Write("panel_control", panel_control_);
    w.Write("power_mode", power_mode_);
    w.Write("vmclk_enable", vmclk_enable_);
    w.Write("vclk_enable", vclk_enable_);
    w.Write("fifo_memory", fifo_memory_);
    w.Write("vertical_half", vertical_half_);
    w.Write("h_display", h_display_);
    w.Write("h_retrace", h_retrace_);
    w.Write("v_end", v_end_);
    w.Write("v_total", v_total_);
    w.Write("v_retrace_start", v_retrace_start_);
    w.Write("v_retrace_end", v_retrace_end_);
    w.Write("start_address", start_address_);
    w.Write("offset", offset_);
    w.Write("cursor_control", cursor_control_);
    w.Write("cursor_x", cursor_x_);
    w.Write("cursor_y", cursor_y_);
    w.Write("cursor_color0a", cursor_color0a_);
    w.Write("cursor_color0b", cursor_color0b_);
    w.Write("cursor_color1a", cursor_color1a_);
    w.Write("cursor_color1b", cursor_color1b_);
    w.Write("cursor_origin", cursor_origin_);
    w.Write("ram_write_addr", ram_write_addr_);
    w.Write("ram_read_addr", ram_read_addr_);
    w.Write("scratch0", scratch_[0]);
    w.Write("scratch1", scratch_[1]);
    w.Write("palette", palette_);
    size_latch_.SaveState(w);
}

void NecVrc4171aLcd::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.Read("panel_select", panel_select_);
    r.Read("panel_control", panel_control_);
    r.Read("power_mode", power_mode_);
    r.Read("vmclk_enable", vmclk_enable_);
    r.Read("vclk_enable", vclk_enable_);
    r.Read("fifo_memory", fifo_memory_);
    r.Read("vertical_half", vertical_half_);
    r.Read("h_display", h_display_);
    r.Read("h_retrace", h_retrace_);
    r.Read("v_end", v_end_);
    r.Read("v_total", v_total_);
    r.Read("v_retrace_start", v_retrace_start_);
    r.Read("v_retrace_end", v_retrace_end_);
    r.Read("start_address", start_address_);
    r.Read("offset", offset_);
    r.Read("cursor_control", cursor_control_);
    r.Read("cursor_x", cursor_x_);
    r.Read("cursor_y", cursor_y_);
    r.Read("cursor_color0a", cursor_color0a_);
    r.Read("cursor_color0b", cursor_color0b_);
    r.Read("cursor_color1a", cursor_color1a_);
    r.Read("cursor_color1b", cursor_color1b_);
    r.Read("cursor_origin", cursor_origin_);
    r.Read("ram_write_addr", ram_write_addr_);
    r.Read("ram_read_addr", ram_read_addr_);
    r.Read("scratch0", scratch_[0]);
    r.Read("scratch1", scratch_[1]);
    r.Read("palette", palette_);
    size_latch_.RestoreState(r);
    ApplyMemoryDecodeLocked();
}

}
