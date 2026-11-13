#include "hp_600_sh7709_microphone.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/byte_order.h>
#include <cerfcore/core/cerf_emulator.h>
#include <cerfcore/core/instance_threads.h>
#include <cerfcore/state/hibernation.h>
#include "../../host/audio_activity_widget.h"
#include "../../socs/sh7709/sh7709_id.h"
#include "hp_600_family_id.h"

namespace cerfcore {

REGISTER_SERVICE(Hp600Sh7709Microphone);

namespace {

constexpr size_t   kMicFifoBytes   = 65536u;
constexpr uint16_t kMicChannels    = 1u;
constexpr uint16_t kMicBits        = 16u;
constexpr int32_t  kPcm16Midpoint  = 0x8000;
constexpr uint32_t kPcm16ToAdShift = 6u;

}

bool Hp600Sh7709Microphone::ShouldRegister() {
    BoardContext& board = emu_.Get<BoardContext>();
    return board.GetBoardFamilyId() == BoardFamilyId::Hp600 && board.GetSocId() == SocId::Sh7709;
}

void Hp600Sh7709Microphone::OnReady() {
    activity_ = &emu_.Get<AudioActivityWidget>();
    activity_->NotePresent();
    mic_.Start(emu_.Get<InstanceThreads>(), "Hp600Microphone", kMicFifoBytes);
    emu_.Get<Hibernation>().RegisterPostRestoreListener([this] { mic_.Clear(); });
}

void Hp600Sh7709Microphone::OnShutdown() { mic_.Stop(); }

void Hp600Sh7709Microphone::SetRate(uint32_t rate_hz) { mic_.Request(rate_hz, kMicChannels, kMicBits); }

uint16_t Hp600Sh7709Microphone::Convert() {
    uint8_t pcm[2];
    if (mic_.Take(pcm, sizeof(pcm)) != sizeof(pcm)) return static_cast<uint16_t>(kPcm16Midpoint >> kPcm16ToAdShift);
    activity_->MarkRx();
    const int32_t sample = static_cast<int16_t>(cerf::le::U16(pcm, 0u));
    return static_cast<uint16_t>((sample + kPcm16Midpoint) >> kPcm16ToAdShift);
}

}
