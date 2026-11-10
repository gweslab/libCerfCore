#include "hp_600_sh7709_key_matrix.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/state/hibernation.h>
#include "../../socs/sh7709/sh7709_id.h"
#include "../../socs/sh77xx/sh77xx_intc.h"
#include "../../socs/sh77xx/sh77xx_pfc.h"
#include "../../socs/sh77xx/sh77xx_port_data.h"
#include "hp_600_family_id.h"

namespace cerfcore {

REGISTER_SERVICE(Hp600Sh7709KeyMatrix);

namespace {

struct RowPin {
    uint32_t port;
    uint32_t pin;
};

constexpr RowPin kRowPins[Hp600Sh7709KeyMatrix::kRows] = {
    {kSh77xxPortD, 1u}, {kSh77xxPortD, 5u}, {kSh77xxPortD, 7u}, {kSh77xxPortE, 0u},
    {kSh77xxPortE, 1u}, {kSh77xxPortE, 3u}, {kSh77xxPortE, 6u}, {kSh77xxPortE, 7u},
};

constexpr uint32_t kPortFColumns = 8u;
constexpr uint16_t kSenseColumns = 0xBFFFu;

constexpr Sh77xxIntcPin SensePin(uint32_t column) {
    return column < kPortFColumns
               ? static_cast<Sh77xxIntcPin>(static_cast<uint32_t>(Sh77xxIntcPin::kPint8) + column)
               : static_cast<Sh77xxIntcPin>(static_cast<uint32_t>(Sh77xxIntcPin::kPint0) + column - kPortFColumns);
}

}

bool Hp600Sh7709KeyMatrix::ShouldRegister() {
    BoardContext& board = emu_.Get<BoardContext>();
    return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
}

void Hp600Sh7709KeyMatrix::OnReady() {
    intc_ = &emu_.Get<Sh77xxIntc>();
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] {
        std::lock_guard<std::mutex> lk(mtx_);
        for (uint16_t& row : pressed_) row = 0u;
        PublishLocked();
    });
}

void Hp600Sh7709KeyMatrix::SetKey(uint32_t cell, bool down) {
    const uint32_t row = cell / kColumns;
    const uint16_t bit = static_cast<uint16_t>(1u << (cell % kColumns));
    std::lock_guard<std::mutex> lk(mtx_);
    pressed_[row] = down ? static_cast<uint16_t>(pressed_[row] | bit) : static_cast<uint16_t>(pressed_[row] & ~bit);
    PublishLocked();
}

void Hp600Sh7709KeyMatrix::TrackRows(const Sh77xxPortData& ports) {
    uint8_t rows_low = 0u;
    for (uint32_t row = 0; row < kRows; ++row) {
        if (ports.Drive(kRowPins[row].port, kRowPins[row].pin) == PinDrive::kLow) {
            rows_low = static_cast<uint8_t>(rows_low | (1u << row));
        }
    }
    std::lock_guard<std::mutex> lk(mtx_);
    rows_low_ = rows_low;
    PublishLocked();
}

void Hp600Sh7709KeyMatrix::PublishLocked() {
    uint16_t low = 0u;
    for (uint32_t row = 0; row < kRows; ++row) {
        if (((rows_low_ >> row) & 1u) != 0u) low = static_cast<uint16_t>(low | pressed_[row]);
    }
    for (uint32_t column = 0; column < kColumns; ++column) {
        if (((kSenseColumns >> column) & 1u) == 0u) continue;
        intc_->SetPinLevel(SensePin(column), ((low >> column) & 1u) == 0u);
    }
}

}
