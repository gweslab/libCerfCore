#include "../../socs/vr41xx/vr41xx_audio_switch.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "compaq_aero_1500_family_id.h"

namespace cerfcore {

namespace {

constexpr Vr41xxAudioPin kSpeakerPins[] = {{33, PinDrive::kHigh}, {34, PinDrive::kHigh}};
constexpr int            kMicGpio       = 32;

class CompaqAero1500AudioSwitch : public Vr41xxGpioAudioSwitch {
public:
    using Vr41xxGpioAudioSwitch::Vr41xxGpioAudioSwitch;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CompaqAero1500;
    }

    bool MicOn() override { return PinAt(kMicGpio, PinDrive::kHigh); }

protected:
    std::span<const Vr41xxAudioPin> SpeakerPins() const override { return kSpeakerPins; }
};

}

REGISTER_SERVICE_AS(CompaqAero1500AudioSwitch, Vr41xxAudioSwitch);

}
