#include "imx51_usb_transceiver_clocks.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include "../../execution/guest_cycle_clock.h"
#include "../chipidea_usb_core.h"
#include "../freescale_module_clocks.h"
#include "imx51_id.h"

#include <utility>

namespace cerfcore {

namespace {

using Ci = ChipIdeaUsbCore;

constexpr uint32_t kPortscPtsUtmi = 0u;

constexpr uint32_t kPhyCtrl0SuspendM      = 1u << 12;
constexpr uint32_t kPhyCtrl0PhyReset      = 1u << 11;
constexpr uint32_t kPhyCtrl0UtmiOnClock   = 1u << 10;
constexpr uint32_t kPhyCtrl0OtgXcvrClkSel = 1u << 7;
constexpr uint32_t kPhyCtrl0H1XcvrClkSel  = 1u << 4;
constexpr uint32_t kUsbCtrl1ExtClkShift   = 24;

}

REGISTER_SERVICE(Imx51UsbTransceiverClocks);

bool Imx51UsbTransceiverClocks::ShouldRegister() {
    return emu_.Get<BoardContext>().GetSocId() == SocId::Imx51;
}

void Imx51UsbTransceiverClocks::OnReady() {
    modules_      = &emu_.Get<FreescaleModuleClocks>();
    timer_clocks_ = &emu_.Get<FreescaleTimerClocks>();
    auto& clock = emu_.Get<GuestCycleClock>();
    modules_->RegisterGateListener([this] { Notify(FreescaleLowPowerMode::kRun); });
    clock.RegisterIdleListener([this] { Notify(timer_clocks_->WfiMode()); });
    clock.RegisterIdleExitListener([this] { Notify(FreescaleLowPowerMode::kRun); });
}

void Imx51UsbTransceiverClocks::RegisterChangeListener(std::function<void(FreescaleLowPowerMode)> fn) {
    listeners_.Add(std::move(fn));
}

void Imx51UsbTransceiverClocks::Notify(FreescaleLowPowerMode mode) {
    listeners_.Call(mode);
}

/* MCIMX51RM Table 60-5 OTG_Xcvr_clk_sel / H1_Xcvr_clk_sel: "Depends on which PHY is connected
   (Serial PHY: ipg_clk_60Mhz; ULPI PHY: ipp_ind_otg_clk; UTMI PHY: sie_clock)". */
bool Imx51UsbTransceiverClocks::CoreClockRuns(uint32_t core, uint32_t portsc, uint32_t phy_ctrl0,
                                              uint32_t usb_ctrl1, FreescaleLowPowerMode mode) const {
    const uint32_t pts = portsc & Ci::kPortscPtsMask;
    const uint32_t sel = core == 0u ? kPhyCtrl0OtgXcvrClkSel
                       : core == 1u ? kPhyCtrl0H1XcvrClkSel : 0u;
    if (sel != 0u && ((phy_ctrl0 & sel) != 0u || pts == Ci::kPortscPtsSerial)) {
        return modules_->ModuleRunsIn(FreescaleModule::kUsboh3Clk60, mode);
    }
    /* MCIMX51RM Table 60-7: "1  Select the clock from external PHY". */
    if (pts == Ci::kPortscPtsUlpi && ((usb_ctrl1 >> (kUsbCtrl1ExtClkShift + core)) & 1u) != 0u) return true;
    if (core == 0u && pts == kPortscPtsUtmi) return UtmiPhyClockOn(portsc, phy_ctrl0, mode);
    emu_.Get<Fatal>().Die("Imx51UsbTransceiverClocks: core %u runs with PORTSC 0x%08X, PHY_CTRL_0 "
                          "0x%08X and USB_CTRL_1 0x%08X; that transceiver clock source is not "
                          "modeled", core, portsc, phy_ctrl0, usb_ctrl1);
}

/* MCIMX51RM Table 7-35 CG0 "usb phy clock"; Table 60-6 pllDivValue: "Selects between 19.2 MHz,
   24 MHz, 26 MHz or 27 MHz reference clock". */
bool Imx51UsbTransceiverClocks::UtmiPhyClockOn(uint32_t portsc, uint32_t phy_ctrl0,
                                               FreescaleLowPowerMode mode) const {
    if (!modules_->ModuleRunsIn(FreescaleModule::kUsbPhy, mode)) return false;
    if ((phy_ctrl0 & kPhyCtrl0PhyReset) != 0u) {
        emu_.Get<Fatal>().Die("Imx51UsbTransceiverClocks: the UTMI PHY clock is asked while "
                              "PHY_CTRL_0 0x%08X holds the PHY in reset", phy_ctrl0);
    }
    /* MCIMX51RM Table 60-52 PHCD: "Writing this bit to a 1b will disable the PHY clock". Table
       60-5 Utmi_on_clock: "available even if suspend is asserted". */
    const bool port_phcd = (portsc & Ci::kPortscPtsMask) == kPortscPtsUtmi && (portsc & Ci::kPortscPhcd) != 0u;
    const bool suspend   = port_phcd || (phy_ctrl0 & kPhyCtrl0SuspendM) == 0u;
    return !suspend || (phy_ctrl0 & kPhyCtrl0UtmiOnClock) != 0u;
}

}
