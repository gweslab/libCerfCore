#include "../../socs/vr41xx/vr41xx_audio_switch.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "nec_cronos3e_family_id.h"

namespace cerfcore {

namespace {

constexpr Vr41xxAudioPin kSpeakerPins[] = {{44, Vr41xxPinDrive::kHigh}};

class NecCronos3eAudioSwitch : public Vr41xxGpioAudioSwitch {
public:
    using Vr41xxGpioAudioSwitch::Vr41xxGpioAudioSwitch;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::NecCronos3e;
    }

    bool MicOn() override { return true; }

protected:
    std::span<const Vr41xxAudioPin> SpeakerPins() const override { return kSpeakerPins; }
};

}

REGISTER_SERVICE_AS(NecCronos3eAudioSwitch, Vr41xxAudioSwitch);

}
