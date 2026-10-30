#include "compaq_ipaq_h3000_micro_keys.h"

#include "compaq_ipaq_h3000_family_id.h"
#include "compaq_ipaq_h3000_micro_frame.h"
#include "../../socs/sa11xx/sa11xx_sp1_uart.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/log.h>
#include <cerfcore/state/emulation_freeze.h>
#include <cerfcore/state/state_stream.h>

namespace cerfcore {

namespace {

constexpr uint8_t kMsgKeyboard = 0x2;
constexpr uint8_t kKeyReleased = 0x80;
constexpr uint8_t kKeyCodeMask = 0x7F;

}

REGISTER_SERVICE(CompaqIpaqH3000MicroKeys);

bool CompaqIpaqH3000MicroKeys::ShouldRegister() {
    auto* bd = emu_.TryGet<BoardContext>();
    return bd && bd->GetBoardFamilyId() == BoardFamilyId::CompaqIpaqH3000;
}

void CompaqIpaqH3000MicroKeys::SetPressed(uint8_t key, bool pressed) {
    const uint8_t code   = static_cast<uint8_t>(key & kKeyCodeMask);
    auto          frozen = emu_.Get<EmulationFreeze>().WorkerSection();
    std::lock_guard<std::mutex> lk(mtx_);
    if ((held_[code] != 0u) == pressed) return;
    held_[code] = pressed ? 1u : 0u;
    SendLocked(code, pressed);
}

void CompaqIpaqH3000MicroKeys::SaveState(StateWriter& w) {
    std::lock_guard<std::mutex> lk(mtx_);
    w.WriteBytes("micro_keys_held", held_.data(), held_.size());
}

void CompaqIpaqH3000MicroKeys::RestoreState(StateReader& r) {
    std::lock_guard<std::mutex> lk(mtx_);
    r.ReadBytes("micro_keys_held", held_.data(), held_.size());
    for (size_t code = 0; code < held_.size(); ++code) {
        if (held_[code] == 0u) continue;
        held_[code] = 0u;
        SendLocked(static_cast<uint8_t>(code), false);
    }
}

void CompaqIpaqH3000MicroKeys::SendLocked(uint8_t key, bool pressed) {
    const uint8_t payload = static_cast<uint8_t>(key | (pressed ? 0u : kKeyReleased));
    uint8_t frame[kCompaqIpaqH3000MicroMaxFrame];
    const size_t n = BuildCompaqIpaqH3000MicroFrame(kMsgKeyboard, &payload, 1u, frame);
    LOG(Board, "CompaqIpaqH3000MicroKeys: key frame 0x%02X\n", payload);
    emu_.Get<Sa11xxSp1Uart>().PushRxBurst(frame, n);
}

}
