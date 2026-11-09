#pragma once

#include <cerfcore/peripherals/peripheral_base.h>

#include "../../lcd/display_size_latch.h"
#include "nec_vrc4171a_lcd_board.h"

#include <array>
#include <cstdint>
#include <mutex>

namespace cerfcore {

class NecVrc4171aFrameMemory;

struct NecVrc4171aLcdScan {
    bool     display_on     = false;
    bool     vdd_on         = false;
    uint32_t color_depth    = 0u;
    uint32_t width          = 0u;
    uint32_t height         = 0u;
    uint32_t start          = 0u;
    uint32_t stride         = 0u;
    uint32_t memory_bytes   = 0u;
    bool     cursor_on      = false;
    uint32_t cursor_base    = 0u;
    int32_t  cursor_left    = 0;
    int32_t  cursor_top     = 0;
    uint16_t cursor_color0a = 0u;
    uint16_t cursor_color0b = 0u;
    uint16_t cursor_color1a = 0u;
    uint16_t cursor_color1b = 0u;
    std::array<uint32_t, 256> palette{};
};

class NecVrc4171aLcd : public Peripheral {
public:
    using Peripheral::Peripheral;

    static constexpr uint32_t kDepth1 = 0u;
    static constexpr uint32_t kDepth2 = 1u;
    static constexpr uint32_t kDepth4 = 2u;
    static constexpr uint32_t kDepth5 = 3u;
    static constexpr uint32_t kDepth6 = 4u;
    static constexpr uint32_t kDepth8 = 5u;
    static constexpr uint32_t kDepth555 = 6u;
    static constexpr uint32_t kDepth565 = 7u;

    static uint32_t PaletteEntry(uint32_t r, uint32_t g, uint32_t b) { return (r << 12) | (g << 6) | b; }
    static uint32_t PaletteRed(uint32_t entry)   { return (entry >> 12) & 0x3Fu; }
    static uint32_t PaletteGreen(uint32_t entry) { return (entry >> 6) & 0x3Fu; }
    static uint32_t PaletteBlue(uint32_t entry)  { return entry & 0x3Fu; }

    bool ShouldRegister() override;
    void OnReady() override;

    uint32_t MmioBase() const override { return register_base_; }
    uint32_t MmioSize() const override;

    uint16_t ReadHalf(uint32_t addr) override;
    void     WriteHalf(uint32_t addr, uint16_t value) override;

    void SaveState(StateWriter& w) override;
    void RestoreState(StateReader& r) override;

    void Scan(NecVrc4171aLcdScan& out);
    bool BacklightOn();
    void PublishPanelEnable(bool visible);

private:
    void     ResetRegisters();
    void     CheckReserved(uint32_t off, uint16_t value, uint16_t reserved);
    void     CheckPanelWiring(uint32_t off, uint16_t value, uint16_t expected);
    void     CheckPanelInterfaceAtEnable(uint16_t power_mode);
    uint32_t ConfiguredMemoryBytes() const;
    void     ApplyMemoryDecodeLocked();

    uint32_t                  register_base_ = 0u;
    NecVrc4171aFrameMemory*   frame_         = nullptr;
    NecVrc4171aPanelInterface panel_interface_;

    std::mutex mtx_;
    uint16_t panel_select_    = 0u;
    uint16_t panel_control_   = 0u;
    uint16_t power_mode_      = 0u;
    uint16_t vmclk_enable_    = 0u;
    uint16_t vclk_enable_     = 0u;
    uint16_t fifo_memory_     = 0u;
    uint16_t vertical_half_   = 0u;
    uint16_t h_display_       = 0u;
    uint16_t h_retrace_       = 0u;
    uint16_t v_end_           = 0u;
    uint16_t v_total_         = 0u;
    uint16_t v_retrace_start_ = 0u;
    uint16_t v_retrace_end_   = 0u;
    uint16_t start_address_   = 0u;
    uint16_t offset_          = 0u;
    uint16_t cursor_control_  = 0u;
    uint16_t cursor_x_        = 0u;
    uint16_t cursor_y_        = 0u;
    uint16_t cursor_color0a_  = 0u;
    uint16_t cursor_color0b_  = 0u;
    uint16_t cursor_color1a_  = 0u;
    uint16_t cursor_color1b_  = 0u;
    uint16_t cursor_origin_   = 0u;
    uint16_t ram_write_addr_  = 0u;
    uint16_t ram_read_addr_   = 0u;
    uint16_t scratch_[2]      = {0u, 0u};
    std::array<uint32_t, 256> palette_{};

    DisplaySizeLatch size_latch_;
};

}
