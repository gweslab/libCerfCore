#include "hp_600_sh7709_touch_panel.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/fatal.h>
#include <cerfcore/host/guest_deep_sleep.h>
#include <cerfcore/state/hibernation.h>
#include "../../peripherals/pin_drive.h"
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_intc.h"
#include "../../socs/sh77xx/sh77xx_pfc.h"
#include "../../socs/sh77xx/sh77xx_port_data.h"
#include "hp_600_family_id.h"

namespace cerfcore {

REGISTER_SERVICE(Hp600Sh7709TouchPanel);

namespace {

constexpr HostPenInput::PanelRange kScreen{38u, 950u, 80u, 900u};
constexpr uint32_t kPlateXDrive = 0u;
constexpr uint32_t kPlateYDrive = 1u;
constexpr uint32_t kScanEnable  = 5u;

}

bool Hp600Sh7709TouchPanel::ShouldRegister() {
    BoardContext& board = emu_.Get<BoardContext>();
    return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
}

void Hp600Sh7709TouchPanel::OnReady() {
    intc_  = &emu_.Get<Sh77xxIntc>();
    ports_ = &emu_.Get<Sh77xxPortData>();
    pens_.Attach(emu_, [this](const HostPenInput::Pen& pen) { DrivePenLine(pen.kind == HostPenInput::kPenDown); });
    emu_.Get<GuestDeepSleep>().RegisterParkClock([this] { pens_.Apply(); });
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
        pens_.Clear();
        DrivePenLine(false);
    });
    DrivePenLine(false);
}

void Hp600Sh7709TouchPanel::PenDown(int x, int y) {
    pens_.Track(x, y);
    pens_.Queue(HostPenInput::Pen{HostPenInput::kPenDown, x, y});
}

void Hp600Sh7709TouchPanel::PenMove(int x, int y) { pens_.Track(x, y); }

void Hp600Sh7709TouchPanel::PenUp() { pens_.Queue(HostPenInput::Pen{HostPenInput::kPenUp, 0, 0}); }

uint16_t Hp600Sh7709TouchPanel::ReadPlate(Plate plate) {
    const PinDrive x_drive = ports_->Drive(kSh77xxPortSc, kPlateXDrive);
    const PinDrive y_drive = ports_->Drive(kSh77xxPortSc, kPlateYDrive);
    const PinDrive scan    = ports_->Drive(kSh77xxPortSc, kScanEnable);
    const bool     y_plate = plate == Plate::kY;
    const PinDrive low     = y_plate ? y_drive : x_drive;
    const PinDrive high    = y_plate ? x_drive : y_drive;
    if (scan == PinDrive::kHigh && low == PinDrive::kLow && high == PinDrive::kHigh) {
        const HostPenInput::Point p     = pens_.Latest();
        uint16_t                  adc_x = 0u;
        uint16_t                  adc_y = 0u;
        pens_.MapToPanel(p.x, p.y, kScreen, &adc_x, &adc_y);
        return y_plate ? adc_y : adc_x;
    }
    emu_.Get<Fatal>().Die("Hp600Sh7709TouchPanel: %s plate read with SCP0 drive %u, SCP1 drive %u, SCP5 drive %u",
                          y_plate ? "Y" : "X", static_cast<unsigned>(x_drive), static_cast<unsigned>(y_drive),
                          static_cast<unsigned>(scan));
}

void Hp600Sh7709TouchPanel::DrivePenLine(bool pen_down) { intc_->SetPinLevel(Sh77xxIntcPin::kIrq3, pen_down); }

}
