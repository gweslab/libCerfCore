#include "../../socs/vr41xx/vr41xx_audio_switch.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "casio_tomcat_family_id.h"

namespace cerfcore {

namespace {

constexpr Vr41xxAudioPin kSpeakerPins[] = {{27, Vr41xxPinDrive::kLow}};

class CasioTomcatAudioSwitch : public Vr41xxGpioAudioSwitch {
public:
    using Vr41xxGpioAudioSwitch::Vr41xxGpioAudioSwitch;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::CasioTomcat;
    }

    bool MicOn() override { return PinAt(37, Vr41xxPinDrive::kLow); }

protected:
    std::span<const Vr41xxAudioPin> SpeakerPins() const override { return kSpeakerPins; }
};

}

REGISTER_SERVICE_AS(CasioTomcatAudioSwitch, Vr41xxAudioSwitch);

}
