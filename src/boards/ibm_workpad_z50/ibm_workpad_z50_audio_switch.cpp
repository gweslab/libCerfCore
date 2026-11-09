#include "../../socs/vr41xx/vr41xx_audio_switch.h"

#include <cerfcore/boards/board_context.h>
#include <cerfcore/core/cerf_emulator.h>
#include "ibm_workpad_z50_family_id.h"
#include "ibm_workpad_z50_gpio_buffers.h"

#include <utility>

namespace cerfcore {

namespace {

constexpr Vr41xxAudioPin kSpeakerPins[] = {{49, Vr41xxPinDrive::kHigh}};

class IbmWorkpadZ50AudioSwitch : public Vr41xxGpioAudioSwitch {
public:
    using Vr41xxGpioAudioSwitch::Vr41xxGpioAudioSwitch;

    bool ShouldRegister() override {
        return emu_.Get<BoardContext>().GetBoardFamilyId() == BoardFamilyId::IbmWorkpadZ50;
    }

    bool MicOn() override { return emu_.Get<IbmWorkpadZ50GpioBuffers>().MicEnabled(); }

    void RegisterChangeListener(std::function<void()> fn) override {
        emu_.Get<IbmWorkpadZ50GpioBuffers>().RegisterOutputListener(fn);
        Vr41xxGpioAudioSwitch::RegisterChangeListener(std::move(fn));
    }

protected:
    std::span<const Vr41xxAudioPin> SpeakerPins() const override { return kSpeakerPins; }
};

}

REGISTER_SERVICE_AS(IbmWorkpadZ50AudioSwitch, Vr41xxAudioSwitch);

}
